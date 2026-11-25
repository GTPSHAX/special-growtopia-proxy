#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>

#if !defined(_WIN32)
#include <sys/select.h>
#endif

#include "net/HttpClient.hpp"
#include "net/Socks5Tcp.hpp"
#include "specialproxy/net/Net.hpp"

namespace {

constexpr int kServerTimeoutSeconds{5};

auto send_all(Socks5Socket socket, const std::string &data) -> void {
  size_t sent{0};
  while (sent < data.size()) {
    const int result = ::send(socket, data.data() + sent,
                              static_cast<int>(data.size() - sent), 0);
    if (result <= 0) {
      return;
    }
    sent += static_cast<size_t>(result);
  }
}

/** Open one loopback listener and report the assigned port. */
auto open_listener(uint16_t &port) -> Socks5Socket {
  const Socks5Socket listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  assert(listener != kInvalidSocks5Socket);
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = 0;
  assert(::bind(listener, reinterpret_cast<sockaddr *>(&address),
                sizeof address) == 0);
  assert(::listen(listener, 4) == 0);
#if defined(_WIN32)
  int length = static_cast<int>(sizeof address);
#else
  socklen_t length = sizeof address;
#endif
  assert(::getsockname(listener, reinterpret_cast<sockaddr *>(&address),
                       &length) == 0);
  port = ntohs(address.sin_port);
  return listener;
}

/** Accept one client with a bounded wait so a failed request cannot hang. */
auto accept_client(Socks5Socket listener) -> Socks5Socket {
  fd_set readable;
  FD_ZERO(&readable);
  FD_SET(listener, &readable);
  timeval timeout{kServerTimeoutSeconds, 0};
  if (::select(static_cast<int>(listener) + 1, &readable, nullptr, nullptr,
               &timeout) <= 0) {
    return kInvalidSocks5Socket;
  }
  return ::accept(listener, nullptr, nullptr);
}

/** Serve one scripted response and close the listener. */
auto serve_once(Socks5Socket listener, const std::string &response) -> void {
  const Socks5Socket client = accept_client(listener);
  if (client != kInvalidSocks5Socket) {
    std::string request;
    char buffer[1024];
    while (request.find("\r\n\r\n") == std::string::npos) {
      const int got = ::recv(client, buffer, sizeof buffer, 0);
      if (got <= 0) {
        break;
      }
      request.append(buffer, static_cast<size_t>(got));
    }
    send_all(client, response);
    close_socks5_socket(client);
  }
  close_socks5_socket(listener);
}

auto test_invalid_url() -> void {
  const auto result = spx::net::http_request("not-a-url", {});
  assert(!result.ok);
  assert(result.status == 0);
  assert(result.error == "invalid URL");
}

auto test_chunked_response() -> void {
  uint16_t port{0};
  const Socks5Socket listener = open_listener(port);
  std::thread server([listener] {
    serve_once(listener, "HTTP/1.1 200 OK\r\n"
                         "Content-Type: text/plain\r\n"
                         "Transfer-Encoding: chunked\r\n"
                         "\r\n"
                         "5\r\nhello\r\n"
                         "6\r\n world\r\n"
                         "0\r\n\r\n");
  });

  spx::net::HttpRequestOptions options;
  options.timeout_ms = 3000;
  const auto result = spx::net::http_request(
      "http://127.0.0.1:" + std::to_string(port) + "/hello", options);
  server.join();

  assert(result.ok);
  assert(result.status == 200);
  assert(result.body == "hello world");
  bool has_type{false};
  for (const auto &[name, value] : result.headers) {
    if (name == "Content-Type") {
      has_type = value == "text/plain";
    }
  }
  assert(has_type);
}

auto test_content_length_response() -> void {
  uint16_t port{0};
  const Socks5Socket listener = open_listener(port);
  std::thread server([listener] {
    serve_once(listener, "HTTP/1.1 201 Created\r\n"
                         "Content-Length: 11\r\n"
                         "\r\n"
                         "hello world");
  });

  spx::net::HttpRequestOptions options;
  options.timeout_ms = 3000;
  options.method = "POST";
  options.body = "payload";
  const auto result = spx::net::http_request(
      "http://127.0.0.1:" + std::to_string(port) + "/submit", options);
  server.join();

  assert(result.ok);
  assert(result.status == 201);
  assert(result.body == "hello world");
}

auto test_response_size_cap() -> void {
  uint16_t port{0};
  const Socks5Socket listener = open_listener(port);
  std::thread server([listener] {
    serve_once(listener, "HTTP/1.1 200 OK\r\n"
                         "Content-Length: 4096\r\n"
                         "\r\n" +
                             std::string(4096, 'x'));
  });

  spx::net::HttpRequestOptions options;
  options.timeout_ms = 3000;
  options.max_bytes = 256;
  const auto result = spx::net::http_request(
      "http://127.0.0.1:" + std::to_string(port) + "/big", options);
  server.join();

  assert(!result.ok);
  assert(result.error == "response too large");
}

/** Optional live check driven by an environment variable. */
auto test_live_request() -> void {
  const char *target = std::getenv("SPXCORE_HTTP_LIVE");
  if (target == nullptr || target[0] == '\0') {
    return;
  }
  spx::net::HttpRequestOptions options;
  options.timeout_ms = 15000;
  options.headers.emplace_back("Accept", "text/html");
  const auto result = spx::net::http_request(target, options);
  assert(result.ok);
  assert(result.status == 200);
  assert(!result.body.empty());
  std::printf("live request ok status=%d bytes=%zu\n", result.status,
              result.body.size());
}

} // namespace

auto main() -> int {
  assert(spx::net::initialize());
  test_invalid_url();
  test_chunked_response();
  test_content_length_response();
  test_response_size_cap();
  test_live_request();
  spx::net::deinitialize();
  return 0;
}
