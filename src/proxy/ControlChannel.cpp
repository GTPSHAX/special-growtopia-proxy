/** @file
 *  @brief Loopback UDP control channel for external upstream updates.
 */

#include "specialproxy/proxy/ControlChannel.hpp"

#include <array>
#include <atomic>
#include <charconv>
#include <cstring>
#include <thread>

#include "specialproxy/utils/Logger.hpp"

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace spx::proxy {
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

constexpr std::string_view kPrefix{"upstream|"};
constexpr size_t kBufferSize{2048};

/** @brief Remove trailing whitespace and line endings from a datagram. */
auto trim_end(std::string_view text) -> std::string_view {
  while (!text.empty()) {
    const char last = text.back();
    if (last == '\n' || last == '\r' || last == ' ' || last == '\t') {
      text.remove_suffix(1);
      continue;
    }
    break;
  }
  return text;
}

} // namespace

auto parse_control_message(std::string_view message, ControlUpstream &upstream)
    -> bool {
  const auto text = trim_end(message);
  if (text.size() <= kPrefix.size() ||
      text.substr(0, kPrefix.size()) != kPrefix) {
    return false;
  }
  const auto rest = text.substr(kPrefix.size());
  const size_t separator = rest.find('|');
  if (separator == std::string_view::npos || separator == 0) {
    return false;
  }
  auto server = rest.substr(0, separator);
  const auto port_text = rest.substr(separator + 1);
  if (port_text.empty() || port_text.find('|') != std::string_view::npos) {
    return false;
  }
  while (!server.empty() && (server.front() == ' ' || server.front() == '\t')) {
    server.remove_prefix(1);
  }
  while (!server.empty() && (server.back() == ' ' || server.back() == '\t')) {
    server.remove_suffix(1);
  }
  if (server.empty() || server.size() > 253) {
    return false;
  }
  uint32_t port{0};
  const auto [end, error] = std::from_chars(
      port_text.data(), port_text.data() + port_text.size(), port);
  if (error != std::errc{} || end != port_text.data() + port_text.size() ||
      port < 1 || port > 65535) {
    return false;
  }
  upstream.server.assign(server);
  upstream.port = static_cast<uint16_t>(port);
  return true;
}

auto send_control_upstream(uint16_t control_port, std::string_view server,
                           uint16_t port) -> bool {
  if (control_port == 0 || server.empty() || port == 0) {
    return false;
  }
  const SocketHandle handle = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (handle == kInvalidSocket) {
    return false;
  }
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(control_port);
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  const std::string message{std::string{kPrefix} + std::string{server} + "|" +
                            std::to_string(port)};
  const auto sent =
      ::sendto(handle, message.data(), static_cast<int>(message.size()), 0,
               reinterpret_cast<const sockaddr *>(&address), sizeof address);
  close_socket(handle);
  return sent == static_cast<int>(message.size());
}

struct ControlChannel::Impl {
  SocketHandle socket{kInvalidSocket};
  Callback callback;
  std::thread thread;
  std::atomic_bool running{false};
};

ControlChannel::ControlChannel() = default;

ControlChannel::~ControlChannel() { stop(); }

ControlChannel::ControlChannel(ControlChannel &&) noexcept = default;

auto ControlChannel::operator=(ControlChannel &&) noexcept
    -> ControlChannel & = default;

auto ControlChannel::start(uint16_t port, Callback callback) -> bool {
  if (port == 0 || impl_ != nullptr) {
    return false;
  }
  auto impl = std::make_unique<Impl>();
  impl->socket = ::socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  if (impl->socket == kInvalidSocket) {
    return false;
  }
  int reuse{1};
  (void)::setsockopt(impl->socket, SOL_SOCKET, SO_REUSEADDR,
                     reinterpret_cast<const char *>(&reuse), sizeof reuse);
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(port);
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  if (::bind(impl->socket, reinterpret_cast<const sockaddr *>(&address),
             sizeof address) < 0) {
    close_socket(impl->socket);
    return false;
  }
#if defined(_WIN32)
  DWORD timeout{200};
#else
  timeval timeout{0, 200000};
#endif
  (void)::setsockopt(impl->socket, SOL_SOCKET, SO_RCVTIMEO,
                     reinterpret_cast<const char *>(&timeout), sizeof timeout);
  impl->callback = std::move(callback);
  impl->running.store(true);
  impl->thread = std::thread([state = impl.get()] {
    while (state->running.load()) {
      std::array<char, kBufferSize> buffer{};
      const auto received =
          ::recvfrom(state->socket, buffer.data(),
                     static_cast<int>(buffer.size() - 1), 0, nullptr, nullptr);
      if (received <= 0) {
        continue;
      }
      const auto message =
          std::string_view{buffer.data(), static_cast<size_t>(received)};
      ControlUpstream update;
      if (!parse_control_message(message, update)) {
        spx::log::warningf("control", "rejected malformed datagram %zu bytes",
                           message.size());
        continue;
      }
      spx::log::infof("control", "upstream update %.*s:%u",
                      static_cast<int>(update.server.size()),
                      update.server.data(), update.port);
      if (state->callback) {
        state->callback(update.server, update.port);
      }
    }
  });
  impl_ = std::move(impl);
  spx::log::infof("control", "listening on 127.0.0.1:%u", port);
  return true;
}

void ControlChannel::stop() {
  if (!impl_) {
    return;
  }
  impl_->running.store(false);
  if (impl_->thread.joinable()) {
    impl_->thread.join();
  }
  if (impl_->socket != kInvalidSocket) {
    close_socket(impl_->socket);
    impl_->socket = kInvalidSocket;
  }
  impl_.reset();
}

auto ControlChannel::running() const noexcept -> bool {
  return impl_ != nullptr && impl_->running.load();
}

} // namespace spx::proxy
