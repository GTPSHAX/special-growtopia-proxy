/** @file
 *  @brief Connection settings for the proxy service.
 */

#ifndef SPXCORE_PROXY_PROXY_CONFIG_HPP
#define SPXCORE_PROXY_PROXY_CONFIG_HPP

#include <cstdint>
#include <string>

#include "specialproxy/net/Socks5.hpp"

namespace spx::proxy {

/** Addresses and ports a ProxyService binds and connects to. */
struct ProxyConfig {
  /** UDP port the Growtopia client connects to. */
  uint16_t listen_port{17099};
  /** Growtopia server the proxy forwards to. */
  std::string server_address{"127.0.0.1"};
  /** UDP port of the Growtopia server. */
  uint16_t server_port{17091};
  /** Maximum simultaneously proxied sessions. */
  size_t max_sessions{64};
  /** Address written into intercepted OnSendToServer redirects so the
   *  client reconnects to the proxy instead of the real server. */
  std::string redirect_address{"127.0.0.1"};
  /** UDP port on 127.0.0.1 for external upstream updates. Zero disables
   *  the control channel. */
  uint16_t control_port{0};
  /** SOCKS5 proxy used for upstream game traffic. Disabled by default. */
  net::Socks5Config socks5;
};

} // namespace spx::proxy

#endif // SPXCORE_PROXY_PROXY_CONFIG_HPP
