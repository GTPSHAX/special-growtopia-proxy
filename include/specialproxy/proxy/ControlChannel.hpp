/** @file
 *  @brief Local UDP control channel for external discovery and relay services.
 */

#ifndef SPXCORE_PROXY_CONTROL_CHANNEL_HPP
#define SPXCORE_PROXY_CONTROL_CHANNEL_HPP

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>

namespace spx::proxy {

/** Endpoint carried by one upstream control datagram. */
struct ControlUpstream {
  std::string server; ///< Game server address.
  uint16_t port{0};   ///< Game server UDP port.
};

/**
 * @brief Parse one upstream control datagram.
 *
 * The accepted form is "upstream|<server>|<port>". One trailing newline and
 * trailing spaces are ignored. Socket initialization is not needed here.
 *
 * @param[in] message Raw datagram bytes.
 * @param[out] upstream Parsed endpoint when the function returns true.
 * @return True when the message is a valid upstream update.
 */
auto parse_control_message(std::string_view message, ControlUpstream& upstream)
    -> bool;

/**
 * @brief Send an upstream update to a control listener on loopback.
 *
 * Sockets must be initialized by the caller before this function is used.
 *
 * @param[in] control_port UDP port of the control listener.
 * @param[in] server Game server address.
 * @param[in] port Game server UDP port.
 * @return True when the datagram was queued.
 */
auto send_control_upstream(uint16_t control_port, std::string_view server,
                           uint16_t port) -> bool;

/** Receives control datagrams and forwards upstream updates. */
class ControlChannel {
 public:
  /** Callback invoked with a validated server address and port. */
  using Callback = std::function<void(std::string_view, uint16_t)>;

  ControlChannel();
  ~ControlChannel();
  ControlChannel(const ControlChannel&) = delete;
  auto operator=(const ControlChannel&) -> ControlChannel& = delete;
  ControlChannel(ControlChannel&&) noexcept;
  auto operator=(ControlChannel&&) noexcept -> ControlChannel&;

  /**
   * @brief Bind loopback and start the receive thread.
   *
   * @param[in] port UDP port to bind on 127.0.0.1.
   * @param[in] callback Invoked for each valid upstream update.
   * @return True when the socket was bound and the thread started.
   */
  auto start(uint16_t port, Callback callback) -> bool;

  /** @brief Stop the receive thread and close the socket. */
  void stop();

  /**
   * @brief Listener state.
   *
   * @return True between a successful start and stop.
   */
  [[nodiscard]] auto running() const noexcept -> bool;

 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace spx::proxy

#endif // SPXCORE_PROXY_CONTROL_CHANNEL_HPP
