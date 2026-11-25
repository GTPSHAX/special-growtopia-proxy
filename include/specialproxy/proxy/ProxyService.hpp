/** @file
 *  @brief Dual host proxy service forwarding between client and server.
 */

#ifndef SPXCORE_PROXY_PROXY_SERVICE_HPP
#define SPXCORE_PROXY_PROXY_SERVICE_HPP

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

#include "specialproxy/net/Net.hpp"
#include "specialproxy/proxy/ProxyConfig.hpp"
#include "specialproxy/proxy/Session.hpp"

namespace spx::proxy {

/** Hook set called around forwarding decisions. */
struct ProxyHooks {
  /** Growtopia client finished connecting to the proxy. */
  std::function<void(Session&)> on_client_connected;
  /** Growtopia server accepted the proxy connection. */
  std::function<void(Session&)> on_server_connected;
  /** Packet from the client, return false to keep it from the server. */
  std::function<bool(Session&, const net::Incoming&)> on_client_packet;
  /** Packet from the server, return false to keep it from the client. */
  std::function<bool(Session&, const net::Incoming&)> on_server_packet;
  /** Either side closed, the whole session is torn down after this. */
  std::function<void(Session&)> on_disconnect;
  /** OnSendToServer intercepted, real target stored and payload rewritten. */
  std::function<void(Session&, const std::string& address, uint16_t port)>
      on_redirect;

  /** @brief Called before each network wait to advance deferred script work.
   *
   * @param[in] timeout_ms Requested maximum wait.
   * @return Effective wait after accounting for pending script wakeups.
   */
  std::function<uint32_t(uint32_t)> on_poll;
};

/** Routes packets between the Growtopia client host and server host. */
class ProxyService {
 public:
  /** @brief Store configuration and hooks without touching the network. */
  ProxyService(ProxyConfig config, ProxyHooks hooks);

  /** @brief Create both ENet hosts.
   *
   * @return False when the listen port could not be bound.
   */
  auto start() -> bool;

  /** @brief Service both hosts once within a timeout window. */
  void poll(uint32_t timeout_ms);

  /** @brief Poll forever at 50 ms windows until stop. */
  void run();

  /** @brief Ask run to return after its current poll. */
  void stop();

  /** @brief Send frame bytes to the client side of one session. */
  auto send_to_client(Session& session, std::span<const std::byte> frame,
                      bool reliable) -> bool;

  /** @brief Send frame bytes to the server side of one session. */
  auto send_to_server(Session& session, std::span<const std::byte> frame,
                      bool reliable) -> bool;

  /** @brief Send frame bytes to the client side of every session. */
  void send_to_all_clients(std::span<const std::byte> frame, bool reliable);

  /** @brief Number of live sessions. */
  [[nodiscard]] auto session_count() const -> size_t;

  /** @brief Enable or disable the OnSendToServer redirect interception.
   *
   * @param enabled True to rewrite redirects toward the proxy.
   */
  void set_intercept_redirect(bool enabled);

  /**
   * @brief Replace the ENet target used by sessions created from now on.
   *
   * The discovery service can advertise a new address and port at any time.
   * Existing sessions keep their original target.
   *
   * @param address Address of the real game server.
   * @param port UDP port of the real game server.
   */
  void set_upstream(std::string_view address, uint16_t port);

  /**
   * @brief Replace the SOCKS5 selection for sessions created from now on.
   *
   * @param[in] config Proxy settings, disabled when config.enabled is false.
   */
  void set_socks5(const net::Socks5Config& config);

 private:
  void handle_client_event(const net::Incoming& event);
  void handle_server_event(Session& session, const net::Incoming& event);
  void tear_down(Session& session);
  void send_upstream_exit();
  auto intercept_redirect(Session& session, const net::Incoming& event)
      -> std::optional<std::vector<std::byte>>;

  ProxyConfig config_;
  ProxyHooks hooks_;
  mutable std::mutex upstream_mutex_;
  std::string upstream_address_;
  uint16_t upstream_port_{0};
  mutable std::mutex socks5_mutex_;
  net::Socks5Config socks5_;
  std::optional<net::Host> client_host_;
  std::unordered_map<ENetPeer*, Session> client_sessions_;
  std::unordered_map<uint32_t, std::pair<std::string, uint16_t>>
      redirects_by_client_ip_;
  uint32_t next_session_id_{1};
  bool intercept_redirect_{true};
  std::atomic<bool> running_{false};
};

} // namespace spx::proxy

#endif // SPXCORE_PROXY_PROXY_SERVICE_HPP
