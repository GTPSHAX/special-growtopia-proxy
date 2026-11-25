/** @file
 *  @brief SOCKS5 TCP CONNECT helper for TCP clients.
 */

#ifndef SPXCORE_NET_SOCKS5_TCP_HPP
#define SPXCORE_NET_SOCKS5_TCP_HPP

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
using Socks5Socket = SOCKET;
constexpr Socks5Socket kInvalidSocks5Socket{INVALID_SOCKET};
inline void close_socks5_socket(Socks5Socket handle) { closesocket(handle); }
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
using Socks5Socket = int;
constexpr Socks5Socket kInvalidSocks5Socket{-1};
inline void close_socks5_socket(Socks5Socket handle) { ::close(handle); }
#endif

#include "specialproxy/net/Socks5.hpp"

namespace spx::net::socks5_tcp {

/**
 * @brief Build the SOCKS5 CONNECT request for a domain target.
 *
 * @param[in] target_host Domain name sent with ATYP 0x03.
 * @param[in] target_port Destination port in network byte order on the wire.
 * @return Complete request bytes.
 */
auto build_connect_request(std::string_view target_host, uint16_t target_port)
    -> std::vector<uint8_t>;

/** Outcome of one SOCKS5 CONNECT exchange. */
struct ConnectResult {
  /** True when the returned socket is ready for TLS. */
  bool ok{false};
  /** Failure reason without credentials. */
  std::string error;
};

/**
 * @brief Connect to a target through SOCKS5 with remote DNS.
 *
 * The target host name is sent with ATYP 0x03, so no local DNS lookup is
 * performed for it. Credentials are never included in the error text.
 *
 * @param[in] target_host Logical destination host name.
 * @param[in] target_port Logical destination TCP port.
 * @param[in] config Proxy selection and optional credentials.
 * @param[in] timeout_ms Timeout for each handshake step.
 * @param[out] socket Connected socket on success.
 * @return Result describing success or a stable failure reason.
 */
auto connect_tcp(std::string_view target_host, uint16_t target_port,
                 const net::Socks5Config &config, int timeout_ms,
                 Socks5Socket &socket) -> ConnectResult;

} // namespace spx::net::socks5_tcp

#endif // SPXCORE_NET_SOCKS5_TCP_HPP
