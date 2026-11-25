/** @file
 *  @brief Minimal synchronous HTTP and HTTPS client.
 */

#include "HttpClient.hpp"

#include <algorithm>
#include <cctype>
#include <charconv>
#include <cstring>

#if !defined(_WIN32)
#include <netdb.h>
#endif

#include <openssl/ssl.h>

#include "Socks5Tcp.hpp"
#include "specialproxy/utils/Logger.hpp"

namespace spx::net {
namespace {

constexpr int kBufferSize{8192};

struct UrlParts {
  bool tls{false};
  std::string host;
  uint16_t port{0};
  std::string target{"/"};
};

auto parse_port(std::string_view text) -> uint16_t {
  uint32_t value{0};
  const auto [end, error] =
      std::from_chars(text.data(), text.data() + text.size(), value);
  if (error != std::errc{} || end != text.data() + text.size() || value == 0 ||
      value > 65535) {
    return 0;
  }
  return static_cast<uint16_t>(value);
}

auto parse_url(std::string_view url, UrlParts &parts) -> bool {
  const size_t scheme_end = url.find("://");
  if (scheme_end == std::string_view::npos) {
    return false;
  }
  const auto scheme = url.substr(0, scheme_end);
  if (scheme != "http" && scheme != "https") {
    return false;
  }
  parts.tls = scheme == "https";
  const auto rest = url.substr(scheme_end + 3);
  const size_t slash = rest.find('/');
  auto authority = rest.substr(0, slash);
  parts.target =
      slash == std::string_view::npos ? "/" : std::string{rest.substr(slash)};
  if (const size_t at = authority.find('@'); at != std::string_view::npos) {
    authority.remove_prefix(at + 1);
  }
  if (authority.empty()) {
    return false;
  }
  if (const size_t colon = authority.rfind(':');
      colon != std::string_view::npos) {
    parts.host.assign(authority.substr(0, colon));
    parts.port = parse_port(authority.substr(colon + 1));
  } else {
    parts.host.assign(authority);
    parts.port = parts.tls ? 443 : 80;
  }
  return !parts.host.empty() && parts.port != 0;
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

auto write_bytes(Socks5Socket socket, SSL *ssl, const char *data, size_t size)
    -> bool {
  size_t sent{0};
  while (sent < size) {
    int result{0};
    if (ssl != nullptr) {
      result = SSL_write(ssl, data + sent, static_cast<int>(size - sent));
    } else {
      result = static_cast<int>(::send(socket, data + sent, size - sent, 0));
    }
    if (result <= 0) {
      return false;
    }
    sent += static_cast<size_t>(result);
  }
  return true;
}

auto read_bytes(Socks5Socket socket, SSL *ssl, char *data, size_t size) -> int {
  if (ssl != nullptr) {
    return SSL_read(ssl, data, static_cast<int>(size));
  }
  return static_cast<int>(::recv(socket, data, size, 0));
}

auto lower(std::string value) -> std::string {
  std::transform(
      value.begin(), value.end(), value.begin(),
      [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
  return value;
}

auto dechunk(std::string_view body) -> std::string {
  std::string out;
  size_t cursor{0};
  while (cursor < body.size()) {
    const size_t eol = body.find("\r\n", cursor);
    if (eol == std::string_view::npos) {
      break;
    }
    uint32_t size{0};
    const auto text = body.substr(cursor, eol - cursor);
    const auto [end, error] =
        std::from_chars(text.data(), text.data() + text.size(), size, 16);
    if (error != std::errc{} || end != text.data() + text.size()) {
      break;
    }
    cursor = eol + 2;
    if (size == 0) {
      break;
    }
    if (cursor + size > body.size()) {
      break;
    }
    out.append(body.substr(cursor, size));
    cursor += size + 2;
  }
  return out;
}

auto fail(std::string message) -> HttpResult {
  return HttpResult{false, 0, {}, {}, std::move(message)};
}

/**
 * @brief Perform one request without reporting the outcome.
 *
 * @param[in] url Absolute http or https URL.
 * @param[in] options Request settings.
 * @return Parsed response or a failure reason.
 */
auto run_request(std::string_view url, const HttpRequestOptions &options)
    -> HttpResult {
  UrlParts parts;
  if (!parse_url(url, parts)) {
    return fail("invalid URL");
  }
  const int timeout = options.timeout_ms > 0 ? options.timeout_ms : 10000;
  spx::log::infof("net", "http %s %s:%u tls=%d socks5=%d timeout=%d",
                  options.method.c_str(), parts.host.c_str(), parts.port,
                  parts.tls ? 1 : 0, options.use_socks5 ? 1 : 0, timeout);

  Socks5Socket fd{kInvalidSocks5Socket};
  if (options.use_socks5 && socks5_validate(options.socks5)) {
    spx::log::infof("net", "http routed via socks5 %s:%u",
                    options.socks5.host.c_str(), options.socks5.port);
    const auto routed = socks5_tcp::connect_tcp(parts.host, parts.port,
                                                options.socks5, timeout, fd);
    if (!routed.ok) {
      return fail(routed.error);
    }
  } else {
    addrinfo hints{};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo *resolved{nullptr};
    if (::getaddrinfo(parts.host.c_str(), nullptr, &hints, &resolved) != 0 ||
        resolved == nullptr) {
      return fail("host resolution failed");
    }
    fd = ::socket(resolved->ai_family, SOCK_STREAM, IPPROTO_TCP);
    if (fd == kInvalidSocks5Socket) {
      freeaddrinfo(resolved);
      return fail("socket failed");
    }
    set_timeout(fd, timeout);
    sockaddr_in address{};
    std::memcpy(&address, resolved->ai_addr, sizeof address);
    address.sin_port = htons(parts.port);
    freeaddrinfo(resolved);
    if (::connect(fd, reinterpret_cast<sockaddr *>(&address), sizeof address) !=
        0) {
      close_socks5_socket(fd);
      return fail("connect failed");
    }
  }
  set_timeout(fd, timeout);

  SSL_CTX *context{nullptr};
  SSL *ssl{nullptr};
  if (parts.tls) {
    context = SSL_CTX_new(TLS_client_method());
    if (context == nullptr) {
      close_socks5_socket(fd);
      return fail("TLS context failed");
    }
    SSL_CTX_set_verify(context, SSL_VERIFY_NONE, nullptr);
    ssl = SSL_new(context);
    if (ssl == nullptr || SSL_set_fd(ssl, static_cast<int>(fd)) != 1 ||
        SSL_set_tlsext_host_name(ssl, parts.host.c_str()) != 1 ||
        SSL_connect(ssl) != 1) {
      if (ssl != nullptr) {
        SSL_free(ssl);
      }
      SSL_CTX_free(context);
      close_socks5_socket(fd);
      return fail("TLS handshake failed");
    }
  }

  std::string request = options.method + " " + parts.target + " HTTP/1.1\r\n";
  request += "Host: " + parts.host + ":" + std::to_string(parts.port) + "\r\n";
  request += "User-Agent: specialproxy-core\r\n";
  request += "Connection: close\r\n";
  for (const auto &[name, value] : options.headers) {
    request += name + ": " + value + "\r\n";
  }
  if (!options.body.empty()) {
    request +=
        "Content-Length: " + std::to_string(options.body.size()) + "\r\n";
  }
  request += "\r\n";
  request += options.body;

  if (!write_bytes(fd, ssl, request.data(), request.size())) {
    if (ssl != nullptr) {
      SSL_free(ssl);
      SSL_CTX_free(context);
    }
    close_socks5_socket(fd);
    return fail("request write failed");
  }

  std::string received;
  char buffer[kBufferSize];
  for (;;) {
    const int got = read_bytes(fd, ssl, buffer, sizeof buffer);
    if (got <= 0) {
      break;
    }
    received.append(buffer, static_cast<size_t>(got));
    if (received.size() > options.max_bytes) {
      if (ssl != nullptr) {
        SSL_free(ssl);
        SSL_CTX_free(context);
      }
      close_socks5_socket(fd);
      return fail("response too large");
    }
    const size_t head_end = received.find("\r\n\r\n");
    if (head_end == std::string::npos) {
      continue;
    }
    const auto head = lower(received.substr(0, head_end));
    const size_t body_start = head_end + 4;
    if (head.find("transfer-encoding: chunked") != std::string::npos) {
      if (received.find("0\r\n\r\n", body_start) != std::string::npos) {
        break;
      }
    } else if (const size_t at = head.find("content-length:");
               at != std::string::npos) {
      const size_t digits = head.find('\n', at);
      const auto value =
          head.substr(at + 15, digits == std::string::npos ? std::string::npos
                                                           : digits - at - 15);
      size_t expected{0};
      const auto [end, error] =
          std::from_chars(value.data(), value.data() + value.size(), expected);
      if (error == std::errc{} && received.size() - body_start >= expected) {
        break;
      }
    }
  }

  if (ssl != nullptr) {
    SSL_free(ssl);
    SSL_CTX_free(context);
  }
  close_socks5_socket(fd);

  const size_t head_end = received.find("\r\n\r\n");
  if (head_end == std::string::npos) {
    return fail("incomplete response");
  }
  HttpResult result;
  result.ok = true;
  const size_t first_line = received.find("\r\n");
  if (first_line != std::string::npos && first_line > 9) {
    const auto code = received.substr(9, first_line - 9);
    const auto [end, error] =
        std::from_chars(code.data(), code.data() + code.size(), result.status);
    (void)end;
    if (error != std::errc{}) {
      result.status = 0;
    }
  }
  size_t cursor{0};
  while (cursor < head_end) {
    const size_t eol = received.find("\r\n", cursor);
    if (eol == std::string::npos || eol > head_end) {
      break;
    }
    const auto line = received.substr(cursor, eol - cursor);
    if (const size_t colon = line.find(':'); colon != std::string::npos) {
      std::string_view value{line};
      value.remove_prefix(colon + 1);
      while (!value.empty() &&
             (value.front() == ' ' || value.front() == '\t')) {
        value.remove_prefix(1);
      }
      while (!value.empty() && (value.back() == '\r' || value.back() == '\n')) {
        value.remove_suffix(1);
      }
      result.headers.emplace_back(line.substr(0, colon), std::string{value});
    }
    cursor = eol + 2;
  }
  result.body = received.substr(head_end + 4);
  if (lower(received.substr(0, head_end)).find("transfer-encoding: chunked") !=
      std::string::npos) {
    result.body = dechunk(result.body);
  }
  return result;
}

} // namespace

auto http_request(std::string_view url, const HttpRequestOptions &options)
    -> HttpResult {
  auto result = run_request(url, options);
  if (result.ok) {
    spx::log::infof("net", "http response status=%d bytes=%zu", result.status,
                    result.body.size());
  } else {
    spx::log::warningf("net", "http request failed %s", result.error.c_str());
  }
  return result;
}

} // namespace spx::net
