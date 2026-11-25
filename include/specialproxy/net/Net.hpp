/** @file
 *  @brief Minimal RAII wrapper over the enet_growtopia C library.
 */

#ifndef SPXCORE_NET_NET_HPP
#define SPXCORE_NET_NET_HPP

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

#include "enet.h"
#include "specialproxy/net/Socks5.hpp"

namespace spx::net {

/**
 * @brief Initialize the ENet library (Winsock/sockets).
 *
 * @return True on success. Must be called once before creating hosts.
 */
auto initialize() -> bool;

/** @brief Release ENet resources. Call once when done with all hosts. */
void deinitialize();

/** ENet wire format role of a host. */
enum class HostRole : uint8_t {
  /** Accept Growtopia clients, uses the server new-header variant. */
  Server,
  /** Connect to a real Growtopia server, uses the client new-header variant. */
  Client,
  /** Plain ENet host for local tests. */
  Standard,
};

/** Kind of host event returned by service. */
enum class EventType : uint8_t {
  None,
  /** A peer finished the ENet handshake. */
  Connect,
  /** A peer left or timed out. */
  Disconnect,
  /** A packet arrived from a peer. */
  Receive,
};

/** Received-data event payload plus the peer it came from. */
struct Incoming {
  /** Event kind, Receive carries data. */
  EventType type{EventType::None};
  /** Peer that sent the data. Valid until the peer disconnects. */
  ENetPeer* peer;
  /** Full packet bytes (message-type prefix included), empty when not Receive. */
  std::vector<std::byte> data;
};

/**
 * @brief Owning ENet host. Move-only, closes the host on destruction.
 */
class Host {
 public:
  Host() = default;

  /**
   * @brief Create a server host bound to a port.
   *
   * @param port UDP port to listen on (0 picks an ephemeral port).
   * @param max_peers Maximum connected peers.
   * @param role Wire format variant used by the host.
   * @return Host on success, nullopt when ENet cannot bind the port.
   */
  static auto create(uint16_t port, size_t max_peers,
                     HostRole role = HostRole::Standard) -> std::optional<Host>;

  /**
   * @brief Start a connection attempt to a remote host.
   *
   * The connection completes when service reports a Connect event for the
   * returned peer.
   *
   * @param address Remote host name or IP in dotted form.
   * @param port Remote UDP port.
   * @return Peer pending connection, or null when ENet refused the request.
   */
  auto connect(const std::string& address, uint16_t port) -> ENetPeer*;

  /**
   * @brief Request a graceful disconnect of one peer.
   *
   * The peer stays valid until service reports its Disconnect event.
   *
   * @param peer Peer to disconnect.
   */
  void disconnect(ENetPeer* peer);

  Host(Host&& other) noexcept;
  auto operator=(Host&& other) noexcept -> Host&;
  Host(const Host&) = delete;
  auto operator=(const Host&) -> Host& = delete;
  ~Host();

  /**
   * @brief Pump the host event loop.
   *
   * @param timeout_ms Max milliseconds to wait, 0 polls and returns at once.
   * @return One event, or nullopt when nothing happened in the window.
   *         Connect/disconnect events carry an empty payload.
   */
  auto service(uint32_t timeout_ms) -> std::optional<Incoming>;

  /** @brief Push queued packets to the socket immediately. */
  void flush();

  /**
   * @brief Route this host's peer traffic through a SOCKS5 UDP relay.
   *
   * Disabled settings clear any existing association. Call before connect.
   *
   * @param[in] config Proxy selection and optional credentials.
   * @return True when the association is ready or was cleared.
   */
  auto use_socks5(const Socks5Config& config) -> bool;

  /**
   * @brief Send bytes to a peer of this host.
   *
   * @param peer Destination peer (from a previous Incoming event).
   * @param data Frame bytes to send.
   * @param reliable Use ENET_PACKET_FLAG_RELIABLE when true.
   * @return True when the packet was queued.
   */
  auto send(ENetPeer* peer, std::span<const std::byte> data, bool reliable)
      -> bool;

 private:
  explicit Host(ENetHost* host) noexcept;

  ENetHost* host_{nullptr};
};

} // namespace

#endif // SPXCORE_NET_NET_HPP
