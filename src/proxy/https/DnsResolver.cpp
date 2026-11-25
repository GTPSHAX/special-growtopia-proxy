/** @file
 *  @brief UDP DNS query and response parsing without the system resolver.
 */

#include "DnsResolver.hpp"

#include <atomic>
#include <cstring>
#include <random>

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace spx::https::dns {
namespace {

#if defined(_WIN32)
using SocketHandle = SOCKET;
constexpr SocketHandle kInvalidSocket{INVALID_SOCKET};

void close_socket(SocketHandle handle) { closesocket(handle); }
#else
using SocketHandle = int;
constexpr SocketHandle kInvalidSocket{-1};

void close_socket(SocketHandle handle) { ::close(handle); }
#endif

constexpr uint16_t kTypeA{1};
constexpr uint16_t kClassIN{1};
constexpr size_t kHeaderSize{12};
constexpr size_t kBufferSize{2048};

auto read_u16(const std::vector<uint8_t> &packet, size_t at) -> uint16_t {
  uint16_t network{0};
  std::memcpy(&network, packet.data() + at, sizeof network);
  return ntohs(network);
}

void append_u16(std::vector<uint8_t> &out, uint16_t value) {
  const uint16_t network = htons(value);
  const auto *bytes = reinterpret_cast<const uint8_t *>(&network);
  out.push_back(bytes[0]);
  out.push_back(bytes[1]);
}

/** @brief Advance past an encoded name, following compression pointers. */
auto skip_name(const std::vector<uint8_t> &packet, size_t &at) -> bool {
  for (;;) {
    if (at >= packet.size()) {
      return false;
    }
    const uint8_t length = packet[at];
    if ((length & 0xC0) == 0xC0) {
      if (at + 1 >= packet.size()) {
        return false;
      }
      at += 2;
      return true;
    }
    if (length == 0) {
      ++at;
      return true;
    }
    if (at + 1 + length > packet.size()) {
      return false;
    }
    at += 1 + length;
  }
}

void apply_timeout(SocketHandle handle, int timeout_ms) {
#if defined(_WIN32)
  const DWORD timeout = static_cast<DWORD>(timeout_ms);
  setsockopt(handle, SOL_SOCKET, SO_RCVTIMEO,
             reinterpret_cast<const char *>(&timeout), sizeof timeout);
  setsockopt(handle, SOL_SOCKET, SO_SNDTIMEO,
             reinterpret_cast<const char *>(&timeout), sizeof timeout);
#else
  timeval timeout{};
  timeout.tv_sec = timeout_ms / 1000;
  timeout.tv_usec = (timeout_ms % 1000) * 1000;
  setsockopt(handle, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout);
  setsockopt(handle, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof timeout);
#endif
}

} // namespace

auto detail::build_query(std::string_view hostname, uint16_t id)
    -> std::optional<std::vector<uint8_t>> {
  if (hostname.empty() || hostname.size() > 253) {
    return std::nullopt;
  }
  std::vector<uint8_t> out;
  out.reserve(kHeaderSize + hostname.size() + 8);
  append_u16(out, id);
  append_u16(out, 0x0100);
  append_u16(out, 1);
  append_u16(out, 0);
  append_u16(out, 0);
  append_u16(out, 0);

  size_t cursor = 0;
  while (cursor < hostname.size()) {
    const size_t dot = hostname.find('.', cursor);
    const size_t end = dot == std::string_view::npos ? hostname.size() : dot;
    const size_t label = end - cursor;
    if (label == 0 || label > 63) {
      return std::nullopt;
    }
    out.push_back(static_cast<uint8_t>(label));
    out.insert(out.end(),
               hostname.begin() + static_cast<std::ptrdiff_t>(cursor),
               hostname.begin() + static_cast<std::ptrdiff_t>(end));
    if (dot == std::string_view::npos) {
      break;
    }
    cursor = dot + 1;
  }
  out.push_back(0);
  append_u16(out, kTypeA);
  append_u16(out, kClassIN);
  return out;
}

auto detail::parse_response(std::span<const uint8_t> packet, uint16_t id)
    -> std::optional<std::string> {
  if (packet.size() < kHeaderSize) {
    return std::nullopt;
  }
  const std::vector<uint8_t> data{packet.begin(), packet.end()};
  if (read_u16(data, 0) != id) {
    return std::nullopt;
  }
  const uint16_t flags = read_u16(data, 2);
  if ((flags & 0x8000) == 0 || (flags & 0x000F) != 0) {
    return std::nullopt;
  }
  const uint16_t questions = read_u16(data, 4);
  const uint16_t answers = read_u16(data, 6);
  if (answers == 0) {
    return std::nullopt;
  }

  size_t at = kHeaderSize;
  for (uint16_t i = 0; i < questions; ++i) {
    if (!skip_name(data, at) || at + 4 > data.size()) {
      return std::nullopt;
    }
    at += 4;
  }
  for (uint16_t i = 0; i < answers; ++i) {
    if (!skip_name(data, at) || at + 10 > data.size()) {
      return std::nullopt;
    }
    const uint16_t type = read_u16(data, at);
    const uint16_t address_class = read_u16(data, at + 2);
    const uint16_t length = read_u16(data, at + 8);
    at += 10;
    if (type == kTypeA && address_class == kClassIN && length == 4) {
      if (at + 4 > data.size()) {
        return std::nullopt;
      }
      char text[INET_ADDRSTRLEN]{};
      const auto *bytes = reinterpret_cast<const uint8_t *>(data.data() + at);
      if (inet_ntop(AF_INET, bytes, text, sizeof text) == nullptr) {
        return std::nullopt;
      }
      return std::string{text};
    }
    if (at + length > data.size()) {
      return std::nullopt;
    }
    at += length;
  }
  return std::nullopt;
}

auto resolve(std::string_view hostname, std::span<const std::string> servers,
             int timeout_ms) -> std::optional<std::string> {
  if (servers.empty()) {
    return std::nullopt;
  }
  static std::atomic<uint16_t> counter{
      static_cast<uint16_t>(std::random_device{}())};
  const uint16_t id = counter.fetch_add(1) + 1;
  const auto query = detail::build_query(hostname, id);
  if (!query) {
    return std::nullopt;
  }

  const SocketHandle handle = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (handle == kInvalidSocket) {
    return std::nullopt;
  }
  const int timeout = timeout_ms > 0 ? timeout_ms : 2000;
  apply_timeout(handle, timeout);

  for (const auto &server : servers) {
    sockaddr_in remote{};
    remote.sin_family = AF_INET;
    remote.sin_port = htons(53);
    if (inet_pton(AF_INET, server.c_str(), &remote.sin_addr) != 1) {
      continue;
    }
    const int sent =
        sendto(handle, reinterpret_cast<const char *>(query->data()),
               static_cast<int>(query->size()), 0,
               reinterpret_cast<sockaddr *>(&remote), sizeof remote);
    if (sent != static_cast<int>(query->size())) {
      continue;
    }

    std::vector<uint8_t> buffer(kBufferSize);
    sockaddr_in from{};
    socklen_t from_length = sizeof from;
    const int received =
        recvfrom(handle, reinterpret_cast<char *>(buffer.data()),
                 static_cast<int>(buffer.size()), 0,
                 reinterpret_cast<sockaddr *>(&from), &from_length);
    if (received <= 0 || from.sin_addr.s_addr != remote.sin_addr.s_addr) {
      continue;
    }
    buffer.resize(static_cast<size_t>(received));
    if (auto address = detail::parse_response(buffer, id)) {
      close_socket(handle);
      return address;
    }
  }
  close_socket(handle);
  return std::nullopt;
}

} // namespace spx::https::dns
