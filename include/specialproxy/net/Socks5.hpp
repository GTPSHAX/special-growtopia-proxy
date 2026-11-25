/** @file
 *  @brief Shared SOCKS5 settings and parsing for proxy services.
 */

#ifndef SPXCORE_NET_SOCKS5_HPP
#define SPXCORE_NET_SOCKS5_HPP

#include <cstdint>
#include <string>
#include <string_view>

namespace spx::net {

/** SOCKS5 proxy selection shared by the core and the HTTPS service. */
struct Socks5Config {
  /** Use the proxy instead of a direct connection. */
  bool enabled{false};
  /** Proxy host name or IPv4 address. */
  std::string host;
  /** Proxy TCP port. */
  uint16_t port{0};
  /** Optional username for RFC 1929 authentication. */
  std::string username;
  /** Optional password for RFC 1929 authentication. */
  std::string password;
};

/**
 * @brief Parse a proxy specification into SOCKS5 settings.
 *
 * Accepted forms are "host:port" and "host:port:username:password". Quotes
 * around individual fields are ignored. Authentication is enabled only when
 * both credentials are present.
 *
 * @param[in] spec Proxy specification.
 * @param[out] config Receives parsed settings on success.
 * @return True when the specification is valid.
 */
auto socks5_parse(std::string_view spec, Socks5Config& config) -> bool;

/**
 * @brief Validate settings before any network operation.
 *
 * @param[in] config Settings to check.
 * @return True when host, port and credential lengths are acceptable.
 */
auto socks5_validate(const Socks5Config& config) -> bool;

} // namespace spx::net

#endif // SPXCORE_NET_SOCKS5_HPP
