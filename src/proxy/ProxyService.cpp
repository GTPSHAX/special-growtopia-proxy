#include "specialproxy/proxy/ProxyService.hpp"

#include "specialproxy/packet/Packet.hpp"
#include "specialproxy/packet/Payload.hpp"
#include "specialproxy/utils/Logger.hpp"

namespace spx::proxy {

namespace {

/** @brief Build a generic text frame used for upstream lifecycle notices. */
auto make_exit_frame(std::string_view body) -> std::vector<std::byte> {
  return packet::serialize_payload(packet::TextPayload{
      static_cast<uint32_t>(packet::NetMessageType::GenericText),
      std::string{body}});
}

/** @brief Build the game log frame shown in the client chat window. */
auto make_greeting_frame() -> std::vector<std::byte> {
  const std::string body = "action|log\nmsg|Connected to `2SpecialProxy";
  return packet::serialize_payload(packet::TextPayload{
      static_cast<uint32_t>(packet::NetMessageType::GameMessage), body});
}

} // namespace

ProxyService::ProxyService(ProxyConfig config, ProxyHooks hooks)
    : config_(std::move(config)), hooks_(std::move(hooks)),
      upstream_address_(config_.server_address),
      upstream_port_(config_.server_port), socks5_(config_.socks5) {}

void ProxyService::set_upstream(std::string_view address, uint16_t port) {
  std::lock_guard<std::mutex> lock(upstream_mutex_);
  upstream_address_.assign(address);
  upstream_port_ = port;
  spx::log::infof("proxy", "upstream set to %.*s:%u",
                  static_cast<int>(address.size()), address.data(), port);
}

void ProxyService::set_socks5(const net::Socks5Config &config) {
  {
    std::lock_guard<std::mutex> lock(socks5_mutex_);
    socks5_ = config;
  }
  spx::log::infof("proxy", "socks5 policy set enabled=%d %s:%u",
                  config.enabled ? 1 : 0, config.host.c_str(), config.port);
}

auto ProxyService::start() -> bool {
  client_host_ = net::Host::create(config_.listen_port, config_.max_sessions,
                                   net::HostRole::Server);
  return client_host_.has_value();
}

void ProxyService::poll(uint32_t timeout_ms) {
  const uint32_t wait =
      hooks_.on_poll ? hooks_.on_poll(timeout_ms) : timeout_ms;
  if (auto event = client_host_->service(wait)) {
    handle_client_event(*event);
  }
  while (auto event = client_host_->service(0)) {
    handle_client_event(*event);
  }
  // Each session owns its upstream host. Growtopia's new-header integrity is
  // derived from the first peer bound to a host, so sharing one host across
  // redirect targets with different ports breaks the handshake.
  for (auto &entry : client_sessions_) {
    Session &session = entry.second;
    if (!session.upstream_host) {
      continue;
    }
    while (auto event = session.upstream_host->service(0)) {
      handle_server_event(session, *event);
    }
  }
}

void ProxyService::run() {
  running_ = true;
  while (running_) {
    poll(50);
  }
  send_upstream_exit();
}

void ProxyService::stop() { running_ = false; }

void ProxyService::send_upstream_exit() {
  const auto quit_to_exit = make_exit_frame("action|quit_to_exit");
  const auto quit = make_exit_frame("action|quit");
  if (quit_to_exit.empty() || quit.empty()) {
    return;
  }

  // Mirrors the upstream shutdown sequence used by luckyproxy. Both notices
  // are sent on the same peer, with a short service window between them.
  size_t notified{0};
  for (auto &[peer, session] : client_sessions_) {
    if (session.server == nullptr || !session.upstream_host) {
      continue;
    }
    if (session.upstream_host->send(session.server, quit_to_exit, true)) {
      session.upstream_host->flush();
      ++notified;
    }
  }
  if (notified == 0) {
    return;
  }
  for (auto &[peer, session] : client_sessions_) {
    if (session.server == nullptr || !session.upstream_host) {
      continue;
    }
    for (int i = 0; i < 5; ++i) {
      session.upstream_host->service(10);
    }
  }
  for (auto &[peer, session] : client_sessions_) {
    if (session.server == nullptr || !session.upstream_host) {
      continue;
    }
    if (session.upstream_host->send(session.server, quit, true)) {
      session.upstream_host->flush();
      spx::log::infof("proxy", "session %u upstream exit sent to %s:%u",
                      session.id, session.server_address.c_str(),
                      session.server_port);
    }
  }
  for (auto &[peer, session] : client_sessions_) {
    if (session.server == nullptr || !session.upstream_host) {
      continue;
    }
    for (int i = 0; i < 10; ++i) {
      session.upstream_host->service(10);
    }
  }
}

auto ProxyService::send_to_client(Session &session,
                                  std::span<const std::byte> frame,
                                  bool reliable) -> bool {
  return session.client != nullptr &&
         client_host_->send(session.client, frame, reliable);
}

auto ProxyService::send_to_server(Session &session,
                                  std::span<const std::byte> frame,
                                  bool reliable) -> bool {
  return session.upstream_host && session.server != nullptr &&
         session.upstream_host->send(session.server, frame, reliable);
}

void ProxyService::send_to_all_clients(std::span<const std::byte> frame,
                                       bool reliable) {
  for (auto &[peer, session] : client_sessions_) {
    client_host_->send(session.client, frame, reliable);
  }
}

auto ProxyService::session_count() const -> size_t {
  return client_sessions_.size();
}

void ProxyService::handle_client_event(const net::Incoming &event) {
  switch (event.type) {
  case net::EventType::Connect: {
    auto [it, inserted] = client_sessions_.emplace(event.peer, Session{});
    Session &session = it->second;
    session.client = event.peer;
    session.id = next_session_id_++;
    // A previously intercepted redirect wins over the configured target.
    auto redirect = redirects_by_client_ip_.find(event.peer->address.host);
    if (redirect != redirects_by_client_ip_.end()) {
      session.server_address = redirect->second.first;
      session.server_port = redirect->second.second;
      redirects_by_client_ip_.erase(redirect);
    } else {
      std::lock_guard<std::mutex> lock(upstream_mutex_);
      session.server_address = upstream_address_;
      session.server_port = upstream_port_;
    }
    if (hooks_.on_client_connected) {
      hooks_.on_client_connected(session);
    }
    const auto greeting = make_greeting_frame();
    if (!greeting.empty() &&
        client_host_->send(session.client, greeting, true)) {
      spx::log::infof("proxy", "session %u greeting sent downstream",
                      session.id);
    } else {
      spx::log::warningf("proxy", "session %u greeting failed", session.id);
    }
    spx::log::infof("proxy", "session %u target %s:%u", session.id,
                    session.server_address.c_str(), session.server_port);
    session.upstream_host = net::Host::create(0, 1, net::HostRole::Client);
    if (!session.upstream_host) {
      spx::log::warningf("proxy", "session %u upstream host failed",
                         session.id);
      break;
    }
    {
      std::lock_guard<std::mutex> lock(socks5_mutex_);
      if (!session.upstream_host->use_socks5(socks5_)) {
        spx::log::warningf("proxy", "session %u socks5 setup failed",
                           session.id);
      }
    }
    session.server = session.upstream_host->connect(session.server_address,
                                                    session.server_port);
    if (session.server != nullptr) {
      spx::log::infof("proxy", "session %u upstream connect requested",
                      session.id);
    } else {
      spx::log::warningf("proxy", "session %u upstream connect failed",
                         session.id);
    }
    break;
  }
  case net::EventType::Receive: {
    auto it = client_sessions_.find(event.peer);
    if (it == client_sessions_.end()) {
      break;
    }
    Session &session = it->second;
    if (hooks_.on_client_packet && !hooks_.on_client_packet(session, event)) {
      break;
    }
    send_to_server(session, event.data, true);
    break;
  }
  case net::EventType::Disconnect: {
    auto it = client_sessions_.find(event.peer);
    if (it == client_sessions_.end()) {
      break;
    }
    Session &session = it->second;
    tear_down(session);
    client_sessions_.erase(it);
    break;
  }
  case net::EventType::None:
    break;
  }
}

void ProxyService::handle_server_event(Session &session,
                                       const net::Incoming &event) {
  switch (event.type) {
  case net::EventType::Connect: {
    if (hooks_.on_server_connected) {
      hooks_.on_server_connected(session);
    }
    break;
  }
  case net::EventType::Receive: {
    // OnSendToServer never reaches the client unmodified.
    auto rewritten = intercept_redirect(session, event);
    std::optional<net::Incoming> rewritten_event;
    if (rewritten) {
      rewritten_event.emplace();
      rewritten_event->peer = event.peer;
      rewritten_event->type = event.type;
      rewritten_event->data = *rewritten;
    }
    const net::Incoming &forwarded = rewritten_event ? *rewritten_event : event;

    if (hooks_.on_server_packet &&
        !hooks_.on_server_packet(session, forwarded)) {
      break;
    }
    client_host_->send(session.client, forwarded.data, true);
    break;
  }
  case net::EventType::Disconnect: {
    spx::log::infof("proxy", "session %u upstream disconnected", session.id);
    session.server = nullptr;
    client_host_->disconnect(session.client);
    // The client Disconnect event finishes the teardown.
    break;
  }
  case net::EventType::None:
    break;
  }
}

void ProxyService::tear_down(Session &session) {
  if (hooks_.on_disconnect) {
    hooks_.on_disconnect(session);
  }
  if (session.server != nullptr) {
    session.upstream_host->disconnect(session.server);
    session.server = nullptr;
  }
  session.upstream_host.reset();
}

void ProxyService::set_intercept_redirect(bool enabled) {
  intercept_redirect_ = enabled;
}

auto ProxyService::intercept_redirect(Session &session,
                                      const net::Incoming &event)
    -> std::optional<std::vector<std::byte>> {
  if (!intercept_redirect_) {
    return std::nullopt;
  }
  auto payload = packet::PacketDecoder::parse_payload(event.data);
  if (!payload) {
    return std::nullopt;
  }
  const auto *variant =
      packet::get_payload_if<packet::VariantPayload>(*payload);
  if (variant == nullptr) {
    return std::nullopt;
  }
  const auto *name = variant->list.get<std::string>(0);
  if (name == nullptr || *name != "OnSendToServer") {
    return std::nullopt;
  }

  // Servers send redirects with either six or seven parameters. Editing the
  // decoded list in place keeps the original parameter count and wire types.
  const auto *signed_port = variant->list.get<int32_t>(1);
  const auto *unsigned_port =
      signed_port == nullptr ? variant->list.get<uint32_t>(1) : nullptr;
  const auto *combined = variant->list.get<std::string>(4);
  if ((signed_port == nullptr && unsigned_port == nullptr) ||
      combined == nullptr) {
    spx::log::warningf("proxy", "session %u OnSendToServer missing fields",
                       session.id);
    return std::nullopt;
  }

  const uint16_t real_port = static_cast<uint16_t>(
      signed_port != nullptr ? *signed_port
                             : static_cast<int32_t>(*unsigned_port));
  const size_t separator = combined->find('|');
  const std::string address = combined->substr(0, separator);
  const std::string suffix = separator == std::string::npos
                                 ? std::string{}
                                 : combined->substr(separator);

  // The client reconnect is keyed by its IP so the new session is routed
  // to the real subserver instead of the configured default target.
  redirects_by_client_ip_[session.client->address.host] = {address, real_port};
  session.server_address = address;
  session.server_port = real_port;

  packet::VariantPayload rewritten;
  rewritten.game_packet = variant->game_packet;
  rewritten.list = variant->list;
  rewritten.list.set(4,
                     packet::VariantValue{config_.redirect_address + suffix});
  if (signed_port != nullptr) {
    rewritten.list.set(
        1, packet::VariantValue{static_cast<int32_t>(config_.listen_port)});
  } else {
    rewritten.list.set(1, packet::VariantValue{config_.listen_port});
  }
  auto frame = packet::serialize_payload(rewritten);
  if (frame.empty()) {
    return std::nullopt;
  }

  spx::log::infof("proxy",
                  "session %u OnSendToServer intercepted, real target %s:%u "
                  "params=%zu",
                  session.id, address.c_str(), real_port,
                  rewritten.list.size());
  if (hooks_.on_redirect) {
    hooks_.on_redirect(session, session.server_address, session.server_port);
  }
  return frame;
}

} // namespace spx::proxy
