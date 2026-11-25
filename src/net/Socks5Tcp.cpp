/** @file
 *  @brief SOCKS5 TCP CONNECT with remote DNS for TCP clients.
 */

#include "Socks5Tcp.hpp"

#include <array>
#include <cstring>
#include <vector>

#if defined(_WIN32)
#include <ws2tcpip.h>
#else
#include <netdb.h>
#endif

#include "specialproxy/utils/Logger.hpp"

namespace spx::net::socks5_tcp {
namespace {

auto fail(std::string message) -> ConnectResult {
  return ConnectResult{false, std::move(message)};
}

auto send_all(Socks5Socket socket, const uint8_t *data, size_t size) -> bool {
  size_t sent{0};
  while (sent < size) {
    const int result =
        ::send(socket, reinterpret_cast<const char *>(data + sent),
               static_cast<int>(size - sent), 0);
    if (result <= 0) {
      return false;
    }
    sent += static_cast<size_t>(result);
  }
  return true;
}

auto recv_all(Socks5Socket socket, uint8_t *data, size_t size) -> bool {
  size_t received{0};
  while (received < size) {
    const int result = ::recv(socket, reinterpret_cast<char *>(data + received),
                              static_cast<int>(size - received), 0);
    if (result <= 0) {
      return false;
    }
    received += static_cast<size_t>(result);
  }
  return true;
}

auto set_timeout(Socks5Socket socket, int timeout_ms) -> void {
#if defined(_WIN32)
  DWORD value = static_cast<DWORD>(timeout_ms);
#else
  timeval value{timeout_ms / 1000, (timeout_ms % 1000) * 1000};
#endif
  (void)::setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO,
                     reinterpret_cast<const char *>(&value), sizeof value);
  (void)::setsockopt(socket, SOL_SOCKET, SO_SNDTIMEO,
                     reinterpret_cast<const char *>(&value), sizeof value);
}

} // namespace

auto build_connect_request(std::string_view target_host, uint16_t target_port)
    -> std::vector<uint8_t> {
  std::vector<uint8_t> request{0x05, 0x01, 0x00, 0x03,
                               static_cast<uint8_t>(target_host.size())};
  request.insert(request.end(), target_host.begin(), target_host.end());
  request.push_back(static_cast<uint8_t>(target_port >> 8));
  request.push_back(static_cast<uint8_t>(target_port & 0xFF));
  return request;
}

auto connect_tcp(std::string_view target_host, uint16_t target_port,
                 const Socks5Config &config, int timeout_ms,
                 Socks5Socket &socket) -> ConnectResult {
  socket = kInvalidSocks5Socket;
  if (!config.enabled || !socks5_validate(config) || target_host.empty() ||
      target_host.size() > 255 || target_port == 0) {
    return fail("invalid socks5 or target settings");
  }

  addrinfo hints{};
  hints.ai_family = AF_INET;
  hints.ai_socktype = SOCK_STREAM;
  addrinfo *resolved{nullptr};
  const std::string proxy_host{config.host};
  if (::getaddrinfo(proxy_host.c_str(), nullptr, &hints, &resolved) != 0 ||
      resolved == nullptr) {
    return fail("socks5 proxy resolution failed");
  }
  Socks5Socket fd = ::socket(resolved->ai_family, SOCK_STREAM, IPPROTO_TCP);
  if (fd == kInvalidSocks5Socket) {
    freeaddrinfo(resolved);
    return fail("socks5 socket failed");
  }
  set_timeout(fd, timeout_ms);
  sockaddr_in address{};
  std::memcpy(&address, resolved->ai_addr, sizeof address);
  address.sin_port = htons(config.port);
  freeaddrinfo(resolved);
  if (::connect(fd, reinterpret_cast<sockaddr *>(&address), sizeof address) !=
      0) {
    close_socks5_socket(fd);
    return fail("socks5 proxy connect failed");
  }

  const bool auth = !config.username.empty();
  std::vector<uint8_t> greeting{0x05, static_cast<uint8_t>(auth ? 0x02 : 0x01),
                                0x00};
  if (auth) {
    greeting.push_back(0x02);
  }
  if (!send_all(fd, greeting.data(), greeting.size())) {
    close_socks5_socket(fd);
    return fail("socks5 negotiation send failed");
  }
  std::array<uint8_t, 2> method{};
  if (!recv_all(fd, method.data(), method.size())) {
    spx::log::infof("net", "socks5 negotiation reply timeout");
    close_socks5_socket(fd);
    return fail("socks5 negotiation reply timeout");
  }
  spx::log::infof("net", "socks5 negotiation reply %02x %02x", method[0],
                  method[1]);
  if (method[0] != 0x05) {
    close_socks5_socket(fd);
    return fail("socks5 negotiation reply invalid");
  }
  if (method[1] == 0x02 && auth) {
    std::vector<uint8_t> request{0x01,
                                 static_cast<uint8_t>(config.username.size())};
    request.insert(request.end(), config.username.begin(),
                   config.username.end());
    request.push_back(static_cast<uint8_t>(config.password.size()));
    request.insert(request.end(), config.password.begin(),
                   config.password.end());
    if (!send_all(fd, request.data(), request.size())) {
      close_socks5_socket(fd);
      return fail("socks5 authentication send failed");
    }
    std::array<uint8_t, 2> reply{};
    if (!recv_all(fd, reply.data(), reply.size())) {
      spx::log::infof("net", "socks5 authentication reply timeout");
      close_socks5_socket(fd);
      return fail("socks5 authentication reply timeout");
    }
    spx::log::infof("net", "socks5 authentication reply %02x %02x", reply[0],
                    reply[1]);
    if (reply[1] != 0x00) {
      close_socks5_socket(fd);
      return fail("socks5 authentication rejected");
    }
  } else if (method[1] != 0x00) {
    close_socks5_socket(fd);
    return fail("socks5 method rejected");
  }

  const auto connect = build_connect_request(target_host, target_port);
  if (!send_all(fd, connect.data(), connect.size())) {
    close_socks5_socket(fd);
    return fail("socks5 connect send failed");
  }
  std::array<uint8_t, 4> header{};
  if (!recv_all(fd, header.data(), header.size())) {
    spx::log::infof("net", "socks5 connect reply timeout");
    close_socks5_socket(fd);
    return fail("socks5 connect reply timeout");
  }
  spx::log::infof("net", "socks5 connect reply %02x %02x %02x %02x", header[0],
                  header[1], header[2], header[3]);
  if (header[0] != 0x05) {
    close_socks5_socket(fd);
    return fail("socks5 connect reply invalid");
  }
  if (header[1] != 0x00) {
    close_socks5_socket(fd);
    return fail("socks5 connect rejected by proxy");
  }
  size_t bound_size{0};
  if (header[3] == 0x01) {
    bound_size = 6;
  } else if (header[3] == 0x03) {
    uint8_t length{0};
    if (!recv_all(fd, &length, 1)) {
      close_socks5_socket(fd);
      return fail("socks5 bound address invalid");
    }
    bound_size = static_cast<size_t>(length) + 2;
  } else if (header[3] == 0x04) {
    bound_size = 18;
  } else {
    close_socks5_socket(fd);
    return fail("socks5 bound address type unsupported");
  }
  std::vector<uint8_t> skip(bound_size);
  if (!recv_all(fd, skip.data(), skip.size())) {
    close_socks5_socket(fd);
    return fail("socks5 bound address truncated");
  }

  socket = fd;
  spx::log::infof("net", "socks5 connect ready %.*s:%u via %s:%u",
                  static_cast<int>(target_host.size()), target_host.data(),
                  target_port, config.host.c_str(), config.port);
  return ConnectResult{true, {}};
}

} // namespace spx::net::socks5_tcp
