/** @file
 *  @brief Parser for the pipe delimited server_data.php response body.
 */

#ifndef SPXCORE_HTTPS_SERVER_DATA_HPP
#define SPXCORE_HTTPS_SERVER_DATA_HPP

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace spx::https {

/** Upstream endpoint advertised by the Growtopia discovery service. */
struct ServerData {
  std::string server; ///< Address from the server field.
  uint16_t port{0};   ///< Value from the port field.
};

/**
 * @brief Extract the server and port fields from a discovery response.
 *
 * Lines use the "key|value" form and may end with LF or CRLF. Both fields
 * must be present and the port must be in the range 1 to 65535.
 *
 * @param body Response body of server_data.php.
 * @return Parsed endpoint, or nullopt on a missing or invalid field.
 */
auto parse_server_data(std::string_view body) -> std::optional<ServerData>;

/**
 * @brief Point the discovery response at the local stream proxy.
 *
 * Replaces the value of the server and port fields while keeping every other
 * line and the original LF or CRLF line endings intact.
 *
 * @param body Response body of server_data.php.
 * @param server Address written into the server field.
 * @param port Port written into the port field.
 * @return Body with the rewritten fields.
 */
auto rewrite_server_data(std::string_view body, std::string_view server,
                         uint16_t port) -> std::string;

} // namespace spx::https

#endif // SPXCORE_HTTPS_SERVER_DATA_HPP
