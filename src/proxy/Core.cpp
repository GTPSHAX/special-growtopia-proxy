#include "specialproxy/proxy/Core.hpp"

#include <algorithm>
#include <atomic>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "../script/LuaEngine.hpp"
#include "../script/LuaPacket.hpp"
#include "../script/ScriptStore.hpp"
#include "item/ItemStore.hpp"
#include "specialproxy/command/BuiltinCommands.hpp"
#include "specialproxy/command/ChatCommandHandler.hpp"
#include "specialproxy/command/CommandRegistry.hpp"
#include "specialproxy/item/ItemData.hpp"
#include "specialproxy/item/ItemDatabase.hpp"
#include "specialproxy/net/Net.hpp"
#include "specialproxy/packet/Packet.hpp"
#include "specialproxy/proxy/ControlChannel.hpp"
#include "specialproxy/proxy/ProxyConfig.hpp"
#include "specialproxy/proxy/ProxyService.hpp"
#include "specialproxy/script/Script.hpp"
#include "specialproxy/state/GameState.hpp"
#include "specialproxy/utils/Logger.hpp"


namespace spx {

namespace {

std::mutex g_state_mutex;
std::mutex g_client_state_mutex;
std::vector<std::byte> g_last_client_state;
std::unique_ptr<proxy::ProxyService> g_service;
std::unique_ptr<proxy::ControlChannel> g_control;
std::atomic<proxy::ProxyService *> g_service_ptr{nullptr};
std::thread g_thread;
std::atomic_bool g_verbose{false};
std::mutex g_upstream_mutex;
std::string g_upstream_server;
uint16_t g_upstream_port{0};
std::mutex g_socks5_mutex;
net::Socks5Config g_socks5;
std::unique_ptr<script::LuaEngine> g_lua;
std::atomic_bool g_script_reload_requested{false};
std::vector<std::string> g_lua_command_names;

/** @brief Hex of the final bytes of a frame for wire diagnostics. */
auto frame_tail(const net::Incoming &event, size_t count) -> std::string {
  static constexpr char kDigits[] = "0123456789abcdef";
  const size_t start =
      event.data.size() > count ? event.data.size() - count : 0;
  std::string out;
  out.reserve((event.data.size() - start) * 3);
  for (size_t i = start; i < event.data.size(); ++i) {
    const auto value = std::to_integer<unsigned char>(event.data[i]);
    out.push_back(kDigits[value >> 4]);
    out.push_back(kDigits[value & 0x0F]);
    out.push_back(' ');
  }
  return out;
}

/** @brief One line summary of a decoded payload for verbose logging. */
void log_packet(const char *direction, const net::Incoming &event) {
  auto payload = packet::PacketDecoder::parse_payload(event.data);
  if (!payload) {
    spx::log::infof("packet", "%s malformed %zu bytes", direction,
                    event.data.size());
    return;
  }
  if (const auto *text =
          packet::get_payload_if<packet::TextPayload>(*payload)) {
    spx::log::infof("packet", "%s text %u %s", direction, text->message_type,
                    text->data.c_str());
    spx::log::infof("packet", "%s text tail bytes=%zu hex=%s", direction,
                    event.data.size(), frame_tail(event, 24).c_str());
  } else if (const auto *variant =
                 packet::get_payload_if<packet::VariantPayload>(*payload)) {
    const auto *name = variant->list.get<std::string>(0);
    spx::log::infof("packet", "%s variant %s params %zu", direction,
                    name != nullptr ? name->c_str() : "<unnamed>",
                    variant->list.size());
  } else if (const auto *game =
                 packet::get_payload_if<packet::GamePayload>(*payload)) {
    spx::log::infof("packet", "%s tank %u net_id %u", direction,
                    game->packet.type, game->packet.net_id);
  } else {
    spx::log::infof("packet", "%s raw %zu bytes", direction, event.data.size());
  }
}

/** @brief Process wide command registry with built-in commands loaded. */
auto command_registry() -> command::CommandRegistry & {
  static command::CommandRegistry registry{'/'};
  static const bool registered = [] {
    command::register_builtin_commands(registry);
    spx::log::infof("command", "registry ready with %zu commands",
                    registry.size());
    return true;
  }();
  (void)registered;
  return registry;
}

/** @brief Process wide chat command handler bound to the shared registry. */
auto command_handler() -> command::ChatCommandHandler & {
  static command::ChatCommandHandler handler{command_registry(),
                                             std::string{version()}};
  return handler;
}

/** @brief Exposes one proxy session to the script engine. */
class CoreScriptHost final : public script::ScriptHost {
public:
  CoreScriptHost(proxy::ProxyService &service, proxy::Session &session)
      : service_{service}, session_{session} {}

  auto send_to_client(std::span<const std::byte> frame, bool reliable)
      -> bool override {
    return service_.send_to_client(session_, frame, reliable);
  }

  auto send_to_server(std::span<const std::byte> frame, bool reliable)
      -> bool override {
    return service_.send_to_server(session_, frame, reliable);
  }

  [[nodiscard]] auto session_id() const -> uint32_t override {
    return session_.id;
  }

  [[nodiscard]] auto server_address() const -> std::string_view override {
    return session_.server_address;
  }

  [[nodiscard]] auto server_port() const -> uint16_t override {
    return session_.server_port;
  }

  [[nodiscard]] auto client_count() const -> size_t override {
    return service_.session_count();
  }

  auto clone() -> std::shared_ptr<script::ScriptHost> override {
    return std::make_shared<CoreScriptHost>(service_, session_);
  }

private:
  proxy::ProxyService &service_;
  proxy::Session &session_;
};

/** @brief Apply a Lua hook decision to the proxy forwarding path. */
auto apply_script_reply(proxy::ProxyService &service, proxy::Session &session,
                        bool to_server, const script::HookReply &reply)
    -> bool {
  if (reply.decision == script::HookDecision::Drop) {
    spx::log::infof("script", "session %u packet dropped by hook", session.id);
    return false;
  }
  if (reply.decision == script::HookDecision::Replaced) {
    spx::log::infof("script", "session %u packet replaced, %zu bytes",
                    session.id, reply.frame.size());
    if (to_server) {
      service.send_to_server(session, reply.frame, true);
    } else {
      service.send_to_client(session, reply.frame, true);
    }
    return false;
  }
  return true;
}

/** @brief Run one Lua packet hook and apply its decision. */
auto run_script_packet(std::string_view event, proxy::ProxyService &service,
                       proxy::Session &session, const net::Incoming &incoming,
                       bool to_server) -> bool {
  auto *lua = g_lua.get();
  if (lua == nullptr) {
    return true;
  }
  CoreScriptHost host{service, session};
  const auto view = script::view_from_frame(incoming.data, session.id);
  const auto reply = lua->on_packet(event, view, host);
  return apply_script_reply(service, session, to_server, reply);
}

/** @brief Notify Lua hooks about a session event without a packet. */
void notify_script(std::string_view event, proxy::Session &session,
                   std::string_view arg1, std::string_view arg2) {
  auto *lua = g_lua.get();
  auto *service = g_service_ptr.load(std::memory_order_acquire);
  if (lua == nullptr || service == nullptr) {
    return;
  }
  CoreScriptHost host{*service, session};
  lua->on_simple(event, session.id, arg1, arg2, host);
}

/** @brief Send a state packet for the tracked local player upstream. */
void send_local_state(proxy::ProxyService &service, proxy::Session &session) {
  std::vector<std::byte> replay;
  {
    std::lock_guard<std::mutex> lock{g_client_state_mutex};
    replay = g_last_client_state;
  }
  if (!replay.empty()) {
    service.send_to_server(session, replay, true);
    spx::log::infof("script",
                    "session %u state packet replayed from client %zu bytes",
                    session.id, replay.size());
    return;
  }

  const int32_t local_id = GameState::instance().local_net_id();
  packet::builder::GameBuilder builder;
  builder.type(packet::PacketType::State);
  if (local_id >= 0) {
    builder.net_id(static_cast<uint32_t>(local_id));
  }
  int32_t tile_x = 0;
  int32_t tile_y = 0;
  const auto player = GameState::instance().players().find(local_id);
  if (player) {
    tile_x = static_cast<int32_t>(player->x / 32.0F);
    tile_y = static_cast<int32_t>(player->y / 32.0F);
    builder.position(player->x, player->y).int_x(tile_x).int_y(tile_y);
    uint32_t flags = packet::PacketFlagOnSolid;
    if (player->facing_left) {
      flags |= packet::PacketFlagRotateLeft;
    }
    builder.flags(flags);
  }
  const auto frame = builder.frame();
  if (!frame.empty()) {
    service.send_to_server(session, frame, true);
    spx::log::infof(
        "script", "session %u state packet sent upstream tile=%d,%d %zu bytes",
        session.id, tile_x, tile_y, frame.size());
  }
}

/** @brief Compare two inventory snapshots into slot ordered changes. */
auto diff_inventory(const std::vector<state::InventoryItem> &before,
                    const std::vector<state::InventoryItem> &after)
    -> std::vector<script::InventoryChange> {
  std::vector<script::InventoryChange> changes;
  const size_t total = (std::max)(before.size(), after.size());
  for (size_t i = 0; i < total; ++i) {
    const auto old = i < before.size() ? before[i] : state::InventoryItem{};
    const auto next = i < after.size() ? after[i] : state::InventoryItem{};
    if (old.id == next.id && old.count == next.count) {
      continue;
    }
    changes.push_back(script::InventoryChange{
        static_cast<uint32_t>(i + 1), old.id, old.count, next.id, next.count});
  }
  return changes;
}

/** @brief Log inventory differences with resolved names and slot usage. */
void log_inventory_changes(uint32_t session_id,
                           const std::vector<script::InventoryChange> &changes,
                           const std::vector<state::InventoryItem> &slots) {
  size_t empty = 0;
  for (const auto &slot : slots) {
    if (slot.id == 0) {
      ++empty;
    }
  }
  std::unordered_map<uint32_t, int32_t> deltas;
  for (const auto &change : changes) {
    if (change.old_id != 0) {
      deltas[change.old_id] -= static_cast<int32_t>(change.old_count);
    }
    if (change.new_id != 0) {
      deltas[change.new_id] += static_cast<int32_t>(change.new_count);
    }
  }
  spx::log::infof(
      "state", "session %u inventory empty=%zu/%zu changed=%zu items=%zu",
      session_id, empty, slots.size(), changes.size(), deltas.size());
  if (changes.size() > 16u) {
    return;
  }
  for (const auto &[id, delta] : deltas) {
    spx::log::infof("state", "session %u inventory %s %s delta=%+d", session_id,
                    delta < 0 ? "-" : "+", spx::item_name(id).c_str(), delta);
  }
}

/** @brief Create the Lua engine and load queued and folder scripts. */
auto load_scripts() -> void {
  g_lua = std::make_unique<script::LuaEngine>();
  if (!g_lua->available()) {
    spx::log::warningf("script", "engine creation failed");
    g_lua.reset();
    return;
  }
  g_lua->set_version(version());
  size_t loaded = 0;
  for (auto &entry : script::take_scripts()) {
    if (g_lua->load(entry.source, entry.name)) {
      ++loaded;
    }
  }

  const auto directory = script::script_directory();
  std::error_code error;
  if (std::filesystem::is_directory(directory, error)) {
    std::vector<std::filesystem::path> files;
    for (const auto &item :
         std::filesystem::directory_iterator{directory, error}) {
      if (error) {
        break;
      }
      if (item.path().extension() == ".lua") {
        files.push_back(item.path());
      }
    }
    std::sort(files.begin(), files.end());
    for (const auto &file : files) {
      std::ifstream input{file, std::ios::binary};
      if (!input) {
        continue;
      }
      const std::string source{std::istreambuf_iterator<char>{input},
                               std::istreambuf_iterator<char>{}};
      if (g_lua->load(source, file.string())) {
        ++loaded;
      }
    }
  }
  g_lua_command_names = g_lua->command_names();
  std::vector<std::string> registered_commands;
  for (const auto &name : g_lua_command_names) {
    if (command_registry().find(name) != nullptr) {
      spx::log::warningf("script", "command /%s already registered",
                         name.c_str());
      continue;
    }
    command_registry().add(
        name, "Lua script command",
        [name](
            const command::CommandContext &context) -> command::CommandResult {
          auto *lua = g_lua.get();
          auto *session =
              static_cast<proxy::Session *>(context.environment().host_session);
          auto *service = g_service_ptr.load(std::memory_order_acquire);
          if (lua == nullptr || session == nullptr || service == nullptr) {
            return command::CommandResult::Failed;
          }
          CoreScriptHost host{*service, *session};
          return lua->invoke_command(name, context.args(), host)
                     ? command::CommandResult::Success
                     : command::CommandResult::Failed;
        });
    spx::log::infof("script", "registered chat command /%s", name.c_str());
    registered_commands.push_back(name);
  }
  g_lua_command_names = std::move(registered_commands);
  spx::log::infof("script", "engine ready with %zu scripts", loaded);
}

/** @brief Reload folder scripts on the proxy thread. */
auto reload_scripts() -> void {
  auto *service = g_service_ptr.load(std::memory_order_acquire);
  if (service == nullptr || g_lua == nullptr) {
    spx::log::warningf("script",
                       "hot reload rejected because proxy is not ready");
    return;
  }
  spx::log::infof("script", "hot reload started");
  const auto directory = script::script_directory();
  std::error_code error;
  std::vector<std::filesystem::path> files;
  if (!std::filesystem::is_directory(directory, error)) {
    spx::log::warningf("script", "hot reload folder unavailable path=%s",
                       directory.c_str());
    return;
  }
  for (const auto &item :
       std::filesystem::directory_iterator{directory, error}) {
    if (error) {
      break;
    }
    if (item.path().extension() == ".lua") {
      files.push_back(item.path());
    }
  }
  std::sort(files.begin(), files.end());

  auto replacement = std::make_unique<script::LuaEngine>();
  if (!replacement->available()) {
    spx::log::warningf("script", "hot reload failed to create engine");
    return;
  }
  replacement->set_version(version());
  {
    std::lock_guard<std::mutex> lock{g_socks5_mutex};
    replacement->inherit_socks5(g_socks5);
  }
  size_t loaded = 0;
  bool load_failed = false;
  for (const auto &file : files) {
    std::ifstream input{file, std::ios::binary};
    if (!input) {
      spx::log::warningf("script", "hot reload cannot read %s",
                         file.string().c_str());
      load_failed = true;
      continue;
    }
    const std::string source{std::istreambuf_iterator<char>{input},
                             std::istreambuf_iterator<char>{}};
    if (replacement->load(source, file.string())) {
      ++loaded;
    } else {
      load_failed = true;
    }
  }
  if (load_failed) {
    spx::log::warningf("script", "hot reload aborted scripts=%zu failed=1",
                       loaded);
    return;
  }
  auto &registry = command_registry();
  for (const auto &name : g_lua_command_names) {
    registry.remove(name);
  }
  g_lua_command_names.clear();
  std::vector<std::string> registered_commands;
  for (const auto &name : replacement->command_names()) {
    if (registry.find(name) != nullptr) {
      spx::log::warningf("script", "command /%s already registered",
                         name.c_str());
      continue;
    }
    registry.add(
        name, "Lua script command",
        [name](
            const command::CommandContext &context) -> command::CommandResult {
          auto *lua = g_lua.get();
          auto *session =
              static_cast<proxy::Session *>(context.environment().host_session);
          auto *current_service = g_service_ptr.load(std::memory_order_acquire);
          if (lua == nullptr || session == nullptr ||
              current_service == nullptr) {
            return command::CommandResult::Failed;
          }
          CoreScriptHost host{*current_service, *session};
          return lua->invoke_command(name, context.args(), host)
                     ? command::CommandResult::Success
                     : command::CommandResult::Failed;
        });
    registered_commands.push_back(name);
  }
  g_lua_command_names = std::move(registered_commands);
  g_lua.swap(replacement);
  spx::log::infof("script", "hot reload complete scripts=%zu commands=%zu",
                  loaded, g_lua_command_names.size());
}

/** @brief Hook set with built-in logging gated by the verbose toggle. */
auto make_hooks() -> proxy::ProxyHooks {
  proxy::ProxyHooks hooks;
  hooks.on_poll = [](uint32_t timeout_ms) -> uint32_t {
    if (g_script_reload_requested.exchange(false, std::memory_order_acq_rel)) {
      reload_scripts();
    }
    auto *lua = g_lua.get();
    if (lua != nullptr && lua->take_reload_request()) {
      g_script_reload_requested.store(true, std::memory_order_release);
    }
    return lua != nullptr ? lua->poll_callback(timeout_ms) : timeout_ms;
  };
  hooks.on_client_connected = [](proxy::Session &session) {
    spx::log::infof("proxy", "session %u client connected", session.id);
    notify_script("onconnect", session, {}, {});
  };
  hooks.on_server_connected = [](proxy::Session &session) {
    spx::log::infof("proxy", "session %u server connected", session.id);
  };
  hooks.on_client_packet = [](proxy::Session &session,
                              const net::Incoming &event) {
    const auto captured = packet::PacketDecoder::parse_payload(event.data);
    const auto *game =
        captured ? packet::get_payload_if<packet::GamePayload>(*captured)
                 : nullptr;
    if (game != nullptr &&
        game->packet.type == static_cast<uint8_t>(packet::PacketType::State)) {
      std::lock_guard<std::mutex> lock{g_client_state_mutex};
      g_last_client_state = event.data;
    }
    auto *service = g_service_ptr.load(std::memory_order_acquire);
    if (service != nullptr &&
        command_handler().consume(*service, session, event)) {
      return false;
    }
    if (service != nullptr &&
        !run_script_packet("onclientpacket", *service, session, event, true)) {
      return false;
    }
    if (g_verbose) {
      log_packet("C2S", event);
    }
    return true;
  };
  hooks.on_server_packet = [](proxy::Session &session,
                              const net::Incoming &event) {
    auto *service = g_service_ptr.load(std::memory_order_acquire);
    auto captured = packet::PacketDecoder::parse_payload(event.data);
    const auto *game_payload =
        captured ? packet::get_payload_if<packet::GamePayload>(*captured)
                 : nullptr;
    const int32_t local_net_id = GameState::instance().local_net_id();
    const bool local_collect =
        game_payload != nullptr && local_net_id >= 0 &&
        game_payload->packet.type ==
            static_cast<uint8_t>(packet::PacketType::ItemChangeObject) &&
        static_cast<int32_t>(game_payload->packet.net_id) == local_net_id;
    const bool inventory_frame =
        (game_payload != nullptr &&
         (game_payload->packet.type ==
              static_cast<uint8_t>(packet::PacketType::SendInventoryState) ||
          game_payload->packet.type ==
              static_cast<uint8_t>(packet::PacketType::ModifyItemInventory))) ||
        local_collect;
    std::vector<state::InventoryItem> before;
    if (inventory_frame) {
      before = GameState::instance().players().inventory_slots();
    }
    GameState::instance().observe(event.data);
    std::vector<script::InventoryChange> inventory_changes;
    if (inventory_frame) {
      const auto after = GameState::instance().players().inventory_slots();
      inventory_changes = diff_inventory(before, after);
      if (!inventory_changes.empty()) {
        log_inventory_changes(session.id, inventory_changes, after);
      }
      if (!inventory_changes.empty() && service != nullptr) {
        auto *lua = g_lua.get();
        if (lua != nullptr) {
          CoreScriptHost host{*service, session};
          lua->on_inventory(session.id, inventory_changes, host);
        }
      }
    }
    if (captured) {
      const auto *game = packet::get_payload_if<packet::GamePayload>(*captured);
      if (game != nullptr &&
          game->packet.type ==
              static_cast<uint8_t>(packet::PacketType::SendItemDatabaseData) &&
          !game->extra.empty()) {
        item::ItemStore::instance().accept(game->extra);
      }
      const auto *variant =
          packet::get_payload_if<packet::VariantPayload>(*captured);
      if (variant != nullptr && service != nullptr) {
        const auto *name = variant->list.get<std::string>(0);
        const auto *dialog = variant->list.get<std::string>(1);
        auto *lua = g_lua.get();
        if (name != nullptr && dialog != nullptr && lua != nullptr &&
            *name == packet::packet_name(packet::PacketID::OnDialogRequest) &&
            lua->consume_dialog_block(*dialog)) {
          spx::log::infof("script",
                          "session %u dialog blocked, sending state upstream",
                          session.id);
          send_local_state(*service, session);
          return false;
        }
        const auto *hash = variant->list.get<uint32_t>(1);
        const int32_t *signed_hash =
            hash == nullptr ? variant->list.get<int32_t>(1) : nullptr;
        const bool super_main =
            name != nullptr &&
            *name == packet::packet_name(
                         packet::PacketID::
                             OnSuperMainStartAcceptLogonHrdxs47254722215a);
        if (super_main && (hash != nullptr || signed_hash != nullptr)) {
          const uint32_t value =
              hash != nullptr ? *hash : static_cast<uint32_t>(*signed_hash);
          if (item::ItemStore::instance().note_upstream_hash(value)) {
            const auto request = packet::serialize_payload(
                packet::TextPayload{2u, "action|refresh_item_data"});
            if (!request.empty() &&
                service->send_to_server(session, request, true)) {
              spx::log::infof("item", "session %u requested upstream items.dat",
                              session.id);
            }
          }
        }
      }
    }
    if (service != nullptr) {
      if (!run_script_packet("onserverpacket", *service, session, event,
                             false)) {
        return false;
      }
      auto *lua = g_lua.get();
      if (lua != nullptr) {
        CoreScriptHost host{*service, session};
        const auto view = script::view_from_frame(event.data, session.id);
        if (view.kind == "variant") {
          const auto reply = lua->on_packet("onvariant", view, host);
          if (!apply_script_reply(*service, session, false, reply)) {
            return false;
          }
        } else if (view.kind == "game") {
          const auto reply = lua->on_packet("ongamepacket", view, host);
          if (!apply_script_reply(*service, session, false, reply)) {
            return false;
          }
        } else if (view.kind == "text") {
          lua->on_simple("ongamemessage", session.id, view.body, {}, host);
        }
      }
    }
    if (g_verbose) {
      log_packet("S2C", event);
    }
    return true;
  };
  hooks.on_redirect = [](proxy::Session &session, const std::string &address,
                         uint16_t port) {
    spx::log::infof("proxy",
                    "session %u OnSendToServer intercepted, real "
                    "target %s:%u",
                    session.id, address.c_str(), port);
    notify_script("onredirect", session, address, std::to_string(port));
  };
  hooks.on_disconnect = [](proxy::Session &session) {
    spx::log::infof("proxy", "session %u disconnected", session.id);
    notify_script("ondisconnect", session, {}, {});
  };
  return hooks;
}

} // namespace

auto version() noexcept -> std::string_view { return SPXCORE_VERSION; }

auto device_info() -> platform::DeviceInfo { return platform::device_info(); }

auto proxy_start(const proxy::ProxyConfig &config) -> bool {
  std::lock_guard<std::mutex> lock(g_state_mutex);
  if (g_service != nullptr) {
    return false;
  }
  if (!net::initialize()) {
    return false;
  }
  spx::log::infof("proxy",
                  "start listen_udp=%u initial_upstream=%s:%u redirect=%s:%u "
                  "(upstream replaced by proxy_set_upstream or redirects)",
                  config.listen_port, config.server_address.c_str(),
                  config.server_port, config.redirect_address.c_str(),
                  config.listen_port);
  command_handler();
  auto service = std::make_unique<proxy::ProxyService>(config, make_hooks());
  if (!service->start()) {
    return false;
  }
  {
    std::lock_guard<std::mutex> upstream_lock{g_upstream_mutex};
    if (!g_upstream_server.empty() && g_upstream_port != 0) {
      service->set_upstream(g_upstream_server, g_upstream_port);
      spx::log::infof("proxy", "applied upstream recorded before start %s:%u",
                      g_upstream_server.c_str(), g_upstream_port);
    }
  }
  {
    std::lock_guard<std::mutex> socks_lock{g_socks5_mutex};
    g_socks5 = config.socks5;
  }
  service->set_intercept_redirect(true);
  load_scripts();
  if (g_lua != nullptr) {
    g_lua->inherit_socks5(config.socks5);
  }
  g_service = std::move(service);
  g_service_ptr.store(g_service.get(), std::memory_order_release);
  g_script_reload_requested.store(false, std::memory_order_release);
  g_thread = std::thread([] {
    if (g_service != nullptr) {
      g_service->run();
    }
  });
  if (config.control_port != 0) {
    auto control = std::make_unique<proxy::ControlChannel>();
    if (control->start(config.control_port,
                       [](std::string_view server, uint16_t port) {
                         (void)proxy_set_upstream(server, port);
                       })) {
      g_control = std::move(control);
    } else {
      spx::log::warningf("control", "failed to bind 127.0.0.1:%u",
                         config.control_port);
    }
  }
  return true;
}

void proxy_stop() {
  spx::log::infof("proxy", "stop");
  std::unique_lock<std::mutex> lock(g_state_mutex);
  if (g_service == nullptr) {
    return;
  }
  g_service_ptr.store(nullptr, std::memory_order_release);
  if (g_control) {
    g_control->stop();
    g_control.reset();
  }
  g_service->stop();
  auto thread = std::move(g_thread);
  auto service = std::move(g_service);
  lock.unlock();
  thread.join();
  lock.lock();
  service.reset();
  g_lua.reset();
  for (const auto &name : g_lua_command_names) {
    command_registry().remove(name);
  }
  g_lua_command_names.clear();
  g_script_reload_requested.store(false, std::memory_order_release);
  net::deinitialize();
}

auto proxy_toggle(std::string_view feature, bool enabled) -> bool {
  if (feature == "verbose") {
    g_verbose = enabled;
    return true;
  }
  std::lock_guard<std::mutex> lock(g_state_mutex);
  if (feature == "intercept_redirect") {
    if (g_service != nullptr) {
      g_service->set_intercept_redirect(enabled);
    }
    return true;
  }
  return false;
}

auto proxy_message_clients(std::string_view message) -> bool {
  std::lock_guard<std::mutex> lock(g_state_mutex);
  if (g_service == nullptr) {
    return false;
  }
  packet::game::OnConsoleMessage packet;
  packet.message.assign(message);
  g_service->send_to_all_clients(
      packet::serialize_payload(packet::Payload{packet.write()}), true);
  return true;
}

auto proxy_set_upstream(std::string_view server, uint16_t port) -> bool {
  {
    std::lock_guard<std::mutex> lock(g_upstream_mutex);
    g_upstream_server.assign(server);
    g_upstream_port = port;
  }
  spx::log::infof("proxy", "upstream supplied via API %.*s:%u",
                  static_cast<int>(server.size()), server.data(), port);
  // The pointer is read without g_state_mutex so an external discovery thread
  // cannot block behind a stop that joins that same thread.
  if (auto *service = g_service_ptr.load(std::memory_order_acquire)) {
    service->set_upstream(server, port);
  }
  return true;
}

auto proxy_upstream() -> UpstreamInfo {
  std::lock_guard<std::mutex> lock(g_upstream_mutex);
  return UpstreamInfo{g_upstream_server, g_upstream_port};
}

auto proxy_set_socks5(const net::Socks5Config &config) -> bool {
  if (config.enabled && !net::socks5_validate(config)) {
    return false;
  }
  {
    std::lock_guard<std::mutex> lock(g_socks5_mutex);
    g_socks5 = config;
  }
  if (auto *service = g_service_ptr.load(std::memory_order_acquire)) {
    service->set_socks5(config);
  }
  if (g_lua != nullptr) {
    g_lua->inherit_socks5(config);
  }
  spx::log::infof("proxy", "socks5 configured enabled=%d host=%s port=%u",
                  config.enabled ? 1 : 0, config.host.c_str(), config.port);
  return true;
}

auto proxy_socks5() -> Socks5Status {
  std::lock_guard<std::mutex> lock(g_socks5_mutex);
  return Socks5Status{g_socks5.enabled, g_socks5.host, g_socks5.port,
                      !g_socks5.username.empty()};
}

} // namespace spx
