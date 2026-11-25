/** @file
 *  @brief SOCKS5 settings parsing shared by core and HTTPS service.
 */

#include "specialproxy/net/Socks5.hpp"

#include <charconv>
#include <vector>

namespace spx::net {
namespace {

/** @brief Remove one pair of surrounding quotes from a field. */
auto unquote(std::string_view value) -> std::string {
  if (value.size() >= 2 && value.front() == '"' && value.back() == '"') {
    value.remove_prefix(1);
    value.remove_suffix(1);
  }
  return std::string{value};
}

/** @brief Split a specification on colons without interpreting quoted text. */
auto split(std::string_view spec) -> std::vector<std::string_view> {
  std::vector<std::string_view> parts;
  size_t start{0};
  while (start <= spec.size()) {
    const size_t end = spec.find(':', start);
    const size_t stop = end == std::string_view::npos ? spec.size() : end;
    parts.push_back(spec.substr(start, stop - start));
    if (end == std::string_view::npos) {
      break;
    }
    start = end + 1;
  }
  return parts;
}

} // namespace

auto socks5_parse(std::string_view spec, Socks5Config &config) -> bool {
  const auto parts = split(spec);
  if (parts.size() != 2 && parts.size() != 4) {
    return false;
  }
  Socks5Config parsed;
  parsed.host = unquote(parts[0]);
  const auto port_text = unquote(parts[1]);
  if (parts.size() == 4) {
    parsed.username = unquote(parts[2]);
    parsed.password = unquote(parts[3]);
  }
  uint32_t port{0};
  const auto [end, error] = std::from_chars(
      port_text.data(), port_text.data() + port_text.size(), port);
  if (error != std::errc{} || end != port_text.data() + port_text.size() ||
      port < 1 || port > 65535) {
    return false;
  }
  parsed.port = static_cast<uint16_t>(port);
  parsed.enabled = true;
  if (!socks5_validate(parsed)) {
    return false;
  }
  config = std::move(parsed);
  return true;
}

auto socks5_validate(const Socks5Config &config) -> bool {
  if (config.host.empty() || config.port == 0) {
    return false;
  }
  if (config.username.find('\0') != std::string::npos ||
      config.password.find('\0') != std::string::npos ||
      config.username.size() > 254 || config.password.size() > 254) {
    return false;
  }
  return config.host.find_first_of("\r\n") == std::string::npos;
}

} // namespace spx::net
