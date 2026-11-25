#include "specialproxy/net/Net.hpp"

#include <cstring>

#include "specialproxy/utils/Logger.hpp"

namespace spx::net {

auto initialize() -> bool { return enet_initialize() == 0; }

void deinitialize() { enet_deinitialize(); }

Host::Host(ENetHost *host) noexcept : host_{host} {}

Host::Host(Host &&other) noexcept : host_{other.host_} {
  other.host_ = nullptr;
}

auto Host::operator=(Host &&other) noexcept -> Host & {
  if (this != &other) {
    if (host_ != nullptr) {
      enet_host_destroy(host_);
    }
    host_ = other.host_;
    other.host_ = nullptr;
  }
  return *this;
}

Host::~Host() {
  if (host_ != nullptr) {
    enet_host_destroy(host_);
  }
}

auto Host::create(uint16_t port, size_t max_peers, HostRole role)
    -> std::optional<Host> {
  ENetAddress address{};
  address.host = ENET_HOST_ANY;
  address.port = port;
  ENetHost *raw =
      enet_host_create(&address, static_cast<int>(max_peers), 2, 0, 0);
  if (raw == nullptr) {
    return std::nullopt;
  }
  if (role != HostRole::Standard) {
    if (enet_host_compress_with_range_coder(raw) != 0) {
      enet_host_destroy(raw);
      return std::nullopt;
    }
    raw->checksum = enet_crc32;
    if (role == HostRole::Server) {
      raw->usingNewPacketForServer = 1;
      spx::log::infof("net", "host server new-header enabled on port %u", port);
    } else {
      raw->usingNewPacket = 1;
      spx::log::infof("net", "host client new-header enabled");
    }
  }
  return Host{raw};
}

auto Host::connect(const std::string &address, uint16_t port) -> ENetPeer * {
  ENetAddress target{};
  enet_address_set_host(&target, address.c_str());
  target.port = port;
  return enet_host_connect(host_, &target, 2, 0);
}

void Host::disconnect(ENetPeer *peer) {
  if (peer != nullptr) {
    enet_peer_disconnect(peer, 0);
  }
}

auto Host::service(uint32_t timeout_ms) -> std::optional<Incoming> {
  ENetEvent event{};
  const int result = enet_host_service(host_, &event, timeout_ms);
  if (result <= 0) {
    return std::nullopt;
  }
  Incoming in;
  in.peer = event.peer;
  switch (event.type) {
  case ENET_EVENT_TYPE_CONNECT:
    in.type = EventType::Connect;
    break;
  case ENET_EVENT_TYPE_DISCONNECT:
    in.type = EventType::Disconnect;
    break;
  case ENET_EVENT_TYPE_RECEIVE: {
    in.type = EventType::Receive;
    if (event.packet != nullptr) {
      const auto *begin =
          reinterpret_cast<const std::byte *>(event.packet->data);
      in.data.assign(begin, begin + event.packet->dataLength);
    }
    break;
  }
  case ENET_EVENT_TYPE_NONE:
  default:
    return std::nullopt;
  }
  return in;
}

void Host::flush() {
  if (host_ != nullptr) {
    enet_host_flush(host_);
  }
}

auto Host::use_socks5(const Socks5Config &config) -> bool {
  if (host_ == nullptr) {
    return false;
  }
  if (!config.enabled) {
    enet_host_disable_socks5(host_);
    return true;
  }
  if (!socks5_validate(config)) {
    return false;
  }
  ENetSocks5Config native{};
  if (enet_address_set_host(&native.address, config.host.c_str()) != 0) {
    return false;
  }
  native.address.port = config.port;
  std::strncpy(native.username, config.username.c_str(),
               sizeof native.username - 1);
  std::strncpy(native.password, config.password.c_str(),
               sizeof native.password - 1);
  if (enet_host_use_socks5(host_, &native) != 0) {
    spx::log::warningf("net", "socks5 association failed for %s:%u",
                       config.host.c_str(), config.port);
    return false;
  }
  spx::log::infof("net", "socks5 udp relay ready %s:%u auth=%d",
                  config.host.c_str(), config.port,
                  config.username.empty() ? 0 : 1);
  return true;
}

auto Host::send(ENetPeer *peer, std::span<const std::byte> data, bool reliable)
    -> bool {
  if (peer == nullptr || data.empty()) {
    return false;
  }
  const enet_uint32 flags =
      reliable ? ENET_PACKET_FLAG_RELIABLE : ENET_PACKET_FLAG_UNSEQUENCED;
  ENetPacket *packet = enet_packet_create(data.data(), data.size(), flags);
  if (packet == nullptr) {
    return false;
  }
  return enet_peer_send(peer, 0, packet) == 0;
}

} // namespace spx::net
