/** @file
 *  @brief Field extraction for the server_data.php response body.
 */

#include "ServerData.hpp"

#include <charconv>
#include <system_error>

namespace spx::https {
namespace {

/** @brief Find one field value in a pipe delimited body. */
auto field_value(std::string_view body, std::string_view key)
    -> std::string_view {
  size_t cursor = 0;
  while (cursor < body.size()) {
    const size_t end = body.find('\n', cursor);
    const size_t stop = end == std::string_view::npos ? body.size() : end;
    std::string_view line = body.substr(cursor, stop - cursor);
    if (!line.empty() && line.back() == '\r') {
      line.remove_suffix(1);
    }
    if (line.size() > key.size() && line.substr(0, key.size()) == key &&
        line[key.size()] == '|') {
      return line.substr(key.size() + 1);
    }
    if (end == std::string_view::npos) {
      break;
    }
    cursor = end + 1;
  }
  return {};
}

} // namespace

auto parse_server_data(std::string_view body) -> std::optional<ServerData> {
  const std::string_view server = field_value(body, "server");
  const std::string_view port_text = field_value(body, "port");
  if (server.empty() || port_text.empty()) {
    return std::nullopt;
  }

  int port{0};
  const auto result = std::from_chars(
      port_text.data(), port_text.data() + port_text.size(), port);
  if (result.ec != std::errc{} ||
      result.ptr != port_text.data() + port_text.size() || port < 1 ||
      port > 65535) {
    return std::nullopt;
  }

  ServerData data;
  data.server.assign(server);
  data.port = static_cast<uint16_t>(port);
  return data;
}

auto rewrite_server_data(std::string_view body, std::string_view server,
                         uint16_t port) -> std::string {
  std::string output;
  output.reserve(body.size() + 64);
  size_t cursor = 0;
  while (cursor < body.size()) {
    size_t end = body.find('\n', cursor);
    if (end == std::string_view::npos) {
      end = body.size();
    }
    const std::string_view line = body.substr(cursor, end - cursor);
    const bool has_cr = !line.empty() && line.back() == '\r';
    const std::string_view key =
        has_cr ? line.substr(0, line.size() - 1) : line;
    if (key.substr(0, 7) == "server|") {
      output += "server|";
      output.append(server);
      if (has_cr) {
        output.push_back('\r');
      }
    } else if (key.substr(0, 5) == "port|") {
      output += "port|";
      output += std::to_string(port);
      if (has_cr) {
        output.push_back('\r');
      }
    } else {
      output.append(line);
    }
    if (end < body.size()) {
      output.push_back('\n');
    }
    cursor = end + 1;
  }
  return output;
}

} // namespace spx::https
