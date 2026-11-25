/** @file
 *  @brief Minimal synchronous HTTP and HTTPS client used by script tasks.
 */

#ifndef SPXCORE_NET_HTTP_CLIENT_HPP
#define SPXCORE_NET_HTTP_CLIENT_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "specialproxy/net/Socks5.hpp"

namespace spx::net {

/** One HTTP header field. */
using HttpHeader = std::pair<std::string, std::string>;

/** Options for one HTTP request. */
struct HttpRequestOptions {
  /** Request verb, upper case. */
  std::string method{"GET"};
  /** Additional headers sent with the request. */
  std::vector<HttpHeader> headers;
  /** Request body bytes. */
  std::string body;
  /** Socket and handshake timeout in milliseconds. */
  int timeout_ms{10000};
  /** Maximum accepted response size in bytes. */
  size_t max_bytes{8u * 1024u * 1024u};
  /** Route the request through the configured SOCKS5 proxy. */
  bool use_socks5{false};
  /** SOCKS5 selection used when use_socks5 is true. */
  Socks5Config socks5;
};

/** Result of one HTTP request. */
struct HttpResult {
  /** True when a complete response was received. */
  bool ok{false};
  /** HTTP status code, zero when the request failed. */
  int status{0};
  /** Response body after transfer decoding. */
  std::string body;
  /** Response header fields. */
  std::vector<HttpHeader> headers;
  /** Short failure reason, empty on success. */
  std::string error;
};

/**
 * @brief Perform one HTTP or HTTPS request.
 *
 * This function blocks the calling thread and is intended for worker threads.
 * TLS certificates are not verified because the client targets end points
 * selected by the script author rather than a fixed trust set.
 *
 * @param[in] url Absolute http or https URL.
 * @param[in] options Method, headers, body, timeout, size cap and proxy.
 * @return Parsed response or a failure reason.
 */
auto http_request(std::string_view url, const HttpRequestOptions &options)
    -> HttpResult;

} // namespace spx::net

#endif // SPXCORE_NET_HTTP_CLIENT_HPP
