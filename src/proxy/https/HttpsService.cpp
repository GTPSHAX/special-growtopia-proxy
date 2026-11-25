/** @file
 *  @brief TLS listener serving HTTP requests with the generated pair.
 */

#include "HttpsService.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <utility>

#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>

#include "specialproxy/platforms/Platform.hpp"
#include "specialproxy/utils/Logger.hpp"

#include "CertificateFactory.hpp"
#include "DnsResolver.hpp"
#include "ServerData.hpp"
#include "net/Socks5Tcp.hpp"

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace spx::https {

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

/** Upper bound for one request including head and body. */
constexpr size_t kMaxRequestBytes{64 * 1024};

/** Upper bound for a captured discovery response used for field parsing. */
constexpr size_t kMaxInspectBytes{1024 * 1024};

struct X509Guard {
  void operator()(X509 *value) const { X509_free(value); }
};

struct PkeyGuard {
  void operator()(EVP_PKEY *value) const { EVP_PKEY_free(value); }
};

using X509Ptr = std::unique_ptr<X509, X509Guard>;
using PkeyPtr = std::unique_ptr<EVP_PKEY, PkeyGuard>;

/** Text behind the status code in the response line. */
auto reason_phrase(unsigned status) -> const char * {
  switch (status) {
  case 200:
    return "OK";
  case 302:
    return "Found";
  case 400:
    return "Bad Request";
  case 403:
    return "Forbidden";
  case 404:
    return "Not Found";
  case 405:
    return "Method Not Allowed";
  case 413:
    return "Payload Too Large";
  case 429:
    return "Too Many Requests";
  case 500:
    return "Internal Server Error";
  case 502:
    return "Bad Gateway";
  default:
    return "Status";
  }
}

/** @brief Reply without invoking the configured hook. */
auto default_response(const HttpRequest &request) -> HttpResponse {
  if (request.method == "GET" && request.path == "/") {
    HttpResponse ok;
    ok.body = "specialproxy https";
    return ok;
  }
  HttpResponse missing;
  missing.status = 404;
  missing.body = "not found";
  return missing;
}

/** @brief Push one response over the established TLS channel. */
void send_response(SSL *ssl, const HttpResponse &response) {
  const std::string head =
      "HTTP/1.1 " + std::to_string(response.status) + " " +
      reason_phrase(response.status) +
      "\r\nContent-Type: " + response.content_type +
      "\r\nContent-Length: " + std::to_string(response.body.size()) +
      "\r\nConnection: close\r\n\r\n";
  SSL_write(ssl, head.data(), static_cast<int>(head.size()));
  if (!response.body.empty()) {
    SSL_write(ssl, response.body.data(),
              static_cast<int>(response.body.size()));
  }
}

/** @brief Split the request line into method, path and query. */
auto parse_request_line(const std::string &raw, size_t head_end,
                        HttpRequest &request) -> bool {
  const size_t line_end = raw.find("\r\n");
  if (line_end == std::string::npos || line_end > head_end) {
    return false;
  }
  const std::string line = raw.substr(0, line_end);
  const size_t space1 = line.find(' ');
  const size_t space2 = line.rfind(' ');
  if (space1 == std::string::npos || space2 == space1) {
    return false;
  }
  request.method = line.substr(0, space1);
  const std::string target = line.substr(space1 + 1, space2 - space1 - 1);
  const size_t query = target.find('?');
  if (query == std::string::npos) {
    request.path = target;
  } else {
    request.path = target.substr(0, query);
    request.query = target.substr(query + 1);
  }
  return true;
}

/** @brief Body size announced by the Content-Length header. */
auto content_length(const std::string &raw, size_t head_end) -> size_t {
  std::string lower = raw.substr(0, head_end);
  for (auto &character : lower) {
    character =
        static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
  }
  const size_t at = lower.find("content-length:");
  if (at == std::string::npos) {
    return 0;
  }
  size_t cursor = at + sizeof("content-length:") - 1;
  size_t value = 0;
  while (cursor < lower.size() && lower[cursor] == ' ') {
    ++cursor;
  }
  while (cursor < lower.size() && lower[cursor] >= '0' &&
         lower[cursor] <= '9') {
    value = value * 10 + static_cast<size_t>(lower[cursor] - '0');
    ++cursor;
  }
  return value;
}

/** @brief True when the response head declares chunked transfer coding. */
auto is_chunked(const std::string &raw, size_t head_end) -> bool {
  std::string lower = raw.substr(0, head_end);
  for (auto &character : lower) {
    character =
        static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
  }
  const size_t at = lower.find("transfer-encoding:");
  if (at == std::string::npos) {
    return false;
  }
  const size_t end = lower.find('\r', at);
  return end != std::string::npos && lower.find("chunked", at) < end;
}

/** @brief Value of a request header, case insensitive. */
auto header_value(const std::string &raw, size_t head_end,
                  std::string_view name) -> std::string {
  std::string lower = raw.substr(0, head_end);
  for (auto &character : lower) {
    character =
        static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
  }
  size_t search = 0;
  while (search < lower.size()) {
    const size_t at = lower.find(name, search);
    if (at == std::string::npos) {
      return {};
    }
    const bool line_start = at == 0 || lower[at - 1] == '\n';
    if (line_start) {
      size_t cursor = at + name.size();
      while (cursor < lower.size() && lower[cursor] == ' ') {
        ++cursor;
      }
      const size_t end = lower.find('\r', cursor);
      if (end == std::string::npos || end > head_end) {
        return {};
      }
      return raw.substr(cursor, end - cursor);
    }
    search = at + name.size();
  }
  return {};
}

/** @brief First comment line carried by a discovery response. */
auto first_comment(std::string_view body) -> std::string {
  size_t cursor = 0;
  while (cursor < body.size()) {
    size_t end = body.find('\n', cursor);
    if (end == std::string_view::npos) {
      end = body.size();
    }
    std::string_view line = body.substr(cursor, end - cursor);
    if (!line.empty() && line.back() == '\r') {
      line.remove_suffix(1);
    }
    if (line.substr(0, 7) == "#maint|") {
      return std::string{line.substr(7)};
    }
    cursor = end + 1;
  }
  return {};
}

/** @brief Update the content length after the body was rewritten. */
auto rewrite_content_length(std::string head, size_t length) -> std::string {
  std::string lower = head;
  for (auto &character : lower) {
    character =
        static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
  }
  const size_t at = lower.find("content-length:");
  if (at != std::string::npos) {
    size_t cursor = at + sizeof("content-length:") - 1;
    while (cursor < lower.size() && lower[cursor] == ' ') {
      ++cursor;
    }
    size_t end = cursor;
    while (end < lower.size() && lower[end] >= '0' && lower[end] <= '9') {
      ++end;
    }
    if (end > cursor) {
      head.replace(cursor, end - cursor, std::to_string(length));
      return head;
    }
  }
  head += "\r\nContent-Length: " + std::to_string(length);
  return head;
}

/** @brief Receive one framed request within the size cap. */
auto receive_request(SSL *ssl, HttpRequest &request) -> bool {
  std::string raw;
  char buffer[4096];
  size_t head_end = std::string::npos;
  while (raw.size() <= kMaxRequestBytes) {
    head_end = raw.find("\r\n\r\n");
    if (head_end != std::string::npos) {
      break;
    }
    const int got = SSL_read(ssl, buffer, sizeof buffer);
    if (got <= 0) {
      return false;
    }
    raw.append(buffer, static_cast<size_t>(got));
  }
  if (head_end == std::string::npos) {
    send_response(ssl, HttpResponse{413, "text/plain", "request too large"});
    return false;
  }
  const size_t body_target = content_length(raw, head_end);
  if (body_target > kMaxRequestBytes) {
    send_response(ssl, HttpResponse{413, "text/plain", "request too large"});
    return false;
  }
  const size_t body_start = head_end + 4;
  while (raw.size() - body_start < body_target) {
    const int got = SSL_read(ssl, buffer, sizeof buffer);
    if (got <= 0) {
      return false;
    }
    raw.append(buffer, static_cast<size_t>(got));
  }
  if (!parse_request_line(raw, head_end, request)) {
    send_response(ssl, HttpResponse{400, "text/plain", "bad request line"});
    return false;
  }
  request.body = raw.substr(body_start);
  request.host = header_value(raw, head_end, "host:");
  request.raw = std::move(raw);
  return true;
}

/** @brief Receive and read timeouts plus latency friendly writes. */
void apply_socket_options(SocketHandle handle) {
  int nodelay{1};
  setsockopt(handle, IPPROTO_TCP, TCP_NODELAY,
             reinterpret_cast<const char *>(&nodelay), sizeof nodelay);
#if defined(_WIN32)
  const DWORD timeout_ms{5000};
  setsockopt(handle, SOL_SOCKET, SO_RCVTIMEO,
             reinterpret_cast<const char *>(&timeout_ms), sizeof timeout_ms);
  setsockopt(handle, SOL_SOCKET, SO_SNDTIMEO,
             reinterpret_cast<const char *>(&timeout_ms), sizeof timeout_ms);
#else
  const timeval timeout{5, 0};
  setsockopt(handle, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout);
  setsockopt(handle, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof timeout);
#endif
#if defined(__APPLE__)
  int no_sigpipe{1};
  setsockopt(handle, SOL_SOCKET, SO_NOSIGPIPE, &no_sigpipe, sizeof no_sigpipe);
#endif
}

} // namespace

struct HttpsService::Slot {
  std::thread thread;
  std::atomic_bool done{false};
};

struct HttpsService::Impl {
  HttpsConfig config;
  HttpsHooks hooks;

  std::mutex mutex;
  std::vector<std::unique_ptr<Slot>> connections;
  std::thread acceptor;
  SSL_CTX *tls{nullptr};
  SSL_CTX *upstream_tls{nullptr};
  std::mutex dns_mutex;
  std::string dns_host;
  std::string dns_address;
  std::mutex socks5_mutex;
  net::Socks5Config socks5;
  SocketHandle listen_fd{kInvalidSocket};
  std::atomic_bool running{false};
  bool hosts_installed{false};
  bool trust_installed{false};
  std::string ca_pem;
  std::vector<std::byte> ca_der;
};

HttpsService::HttpsService(HttpsConfig config, HttpsHooks hooks) {
  impl_ = std::make_unique<Impl>();
  impl_->config = std::move(config);
  impl_->hooks = std::move(hooks);
  impl_->socks5 = impl_->config.socks5;
}

void HttpsService::set_socks5(const net::Socks5Config &config) {
  {
    std::lock_guard<std::mutex> lock(impl_->socks5_mutex);
    impl_->socks5 = config;
  }
  spx::log::infof("https", "socks5 policy set enabled=%d host=%s port=%u",
                  config.enabled ? 1 : 0, config.host.c_str(), config.port);
}

HttpsService::~HttpsService() { stop(); }

auto HttpsService::running() const noexcept -> bool {
  return impl_->running.load();
}

auto HttpsService::ca_pem() const -> std::string { return impl_->ca_pem; }

void HttpsService::purge_finished() {
  Impl &self = *impl_;
  std::lock_guard<std::mutex> lock(self.mutex);
  for (auto it = self.connections.begin(); it != self.connections.end();) {
    if ((*it)->done.load()) {
      (*it)->thread.join();
      it = self.connections.erase(it);
    } else {
      ++it;
    }
  }
}

void HttpsService::connection_thread(std::uint64_t raw_handle, Slot *slot) {
  const SocketHandle handle = static_cast<SocketHandle>(raw_handle);
  apply_socket_options(handle);
  SSL *ssl = SSL_new(impl_->tls);
  const bool handshake = ssl != nullptr &&
                         SSL_set_fd(ssl, static_cast<int>(handle)) == 1 &&
                         SSL_accept(ssl) == 1;
  if (!handshake) {
    spx::log::infof("https", "TLS handshake failed");
  }
  if (handshake) {
    HttpRequest request;
    if (receive_request(ssl, request)) {
      spx::log::infof("https", "request %s %s host=%s", request.method.c_str(),
                      request.path.c_str(), request.host.c_str());
      if (impl_->config.reverse_proxy) {
        relay_to_upstream(ssl, request);
      } else {
        const HttpResponse response = impl_->hooks.on_request
                                          ? impl_->hooks.on_request(request)
                                          : default_response(request);
        send_response(ssl, response);
      }
    } else {
      spx::log::infof("https", "incomplete request %s %s",
                      request.method.c_str(), request.path.c_str());
    }
  }
  if (ssl != nullptr) {
    SSL_shutdown(ssl);
    SSL_free(ssl);
  }
  close_socket(handle);
  slot->done.store(true);
}

void HttpsService::relay_to_upstream(SSL *client, const HttpRequest &request) {
  Impl &self = *impl_;
  std::string host =
      request.host.empty() ? self.config.upstream_host : request.host;
  const size_t colon = host.find(':');
  if (colon != std::string::npos) {
    host.resize(colon);
  }

  net::Socks5Config socks5;
  {
    std::lock_guard<std::mutex> lock(self.socks5_mutex);
    socks5 = self.socks5;
  }

  std::string address = self.config.upstream_address_override;
  if (!socks5.enabled && address.empty()) {
    {
      std::lock_guard<std::mutex> lock(self.dns_mutex);
      if (self.dns_host == host && !self.dns_address.empty()) {
        address = self.dns_address;
        spx::log::infof("dns", "%s -> %s (cached)", host.c_str(),
                        address.c_str());
      }
    }
    if (address.empty()) {
      const auto resolved = dns::resolve(host, self.config.dns_servers,
                                         self.config.dns_timeout_ms);
      if (resolved) {
        std::lock_guard<std::mutex> lock(self.dns_mutex);
        self.dns_host = host;
        self.dns_address = *resolved;
        address = *resolved;
        spx::log::infof("dns", "%s -> %s (resolved)", host.c_str(),
                        address.c_str());
      }
    }
    if (address.empty()) {
      spx::log::infof("dns", "%s resolution failed", host.c_str());
      if (self.hooks.on_warning) {
        self.hooks.on_warning("cannot resolve " + host);
      }
      send_response(client, HttpResponse{502, "text/plain", "bad gateway"});
      return;
    }
  }

  SocketHandle fd{kInvalidSocket};
  if (socks5.enabled) {
    spx::log::infof("https", "upstream via socks5 %s:%u sni=%s",
                    socks5.host.c_str(), socks5.port, host.c_str());
    const int socks5_timeout =
        (std::max)(self.config.dns_timeout_ms * 5, 10000);
    const auto result = net::socks5_tcp::connect_tcp(
        host, self.config.upstream_port, socks5, socks5_timeout, fd);
    if (!result.ok || self.upstream_tls == nullptr) {
      spx::log::infof("https", "upstream socks5 failed %s",
                      result.error.c_str());
      if (self.hooks.on_warning) {
        self.hooks.on_warning("socks5 " + result.error);
      }
      if (fd != kInvalidSocks5Socket) {
        close_socks5_socket(fd);
      }
      send_response(client, HttpResponse{502, "text/plain", "bad gateway"});
      return;
    }
  } else {
    spx::log::infof("https", "upstream connect %s:%u sni=%s", address.c_str(),
                    self.config.upstream_port, host.c_str());
    fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd == kInvalidSocket) {
      spx::log::infof("https", "upstream socket failed");
      send_response(client, HttpResponse{502, "text/plain", "bad gateway"});
      return;
    }
    apply_socket_options(fd);
    sockaddr_in remote{};
    remote.sin_family = AF_INET;
    remote.sin_port = htons(self.config.upstream_port);
    if (inet_pton(AF_INET, address.c_str(), &remote.sin_addr) != 1 ||
        connect(fd, reinterpret_cast<sockaddr *>(&remote), sizeof remote) !=
            0 ||
        self.upstream_tls == nullptr) {
      spx::log::infof("https", "upstream connect failed %s:%u", address.c_str(),
                      self.config.upstream_port);
      close_socket(fd);
      send_response(client, HttpResponse{502, "text/plain", "bad gateway"});
      return;
    }
  }

  SSL *upstream = SSL_new(self.upstream_tls);
  if (upstream == nullptr) {
    spx::log::infof("https", "upstream SSL allocation failed");
    close_socket(fd);
    send_response(client, HttpResponse{502, "text/plain", "bad gateway"});
    return;
  }
  const bool connected =
      SSL_set_fd(upstream, static_cast<int>(fd)) == 1 &&
      SSL_set_tlsext_host_name(upstream, host.c_str()) == 1 &&
      SSL_connect(upstream) == 1;
  if (!connected) {
    spx::log::infof("https", "upstream TLS handshake failed %s:%u sni=%s",
                    address.c_str(), self.config.upstream_port, host.c_str());
    SSL_free(upstream);
    close_socket(fd);
    send_response(client, HttpResponse{502, "text/plain", "bad gateway"});
    return;
  }

  const int written = SSL_write(upstream, request.raw.data(),
                                static_cast<int>(request.raw.size()));
  if (written <= 0 || static_cast<size_t>(written) != request.raw.size()) {
    spx::log::infof("https",
                    "upstream request write failed written=%d size=%zu",
                    written, request.raw.size());
  }
  if (written > 0 && static_cast<size_t>(written) == request.raw.size()) {
    const bool inspect = request.path == "/growtopia/server_data.php";
    const bool rewrite = inspect && self.config.rewrite_server_data &&
                         self.config.stream_port != 0;
    std::string captured;
    std::string status_line;
    bool overflow{false};
    bool status_done{false};
    bool framed{false};
    bool chunked{false};
    size_t head_end{std::string::npos};
    size_t body_expected{0};
    size_t upstream_bytes{0};
    char buffer[8192];
    for (;;) {
      const int got = SSL_read(upstream, buffer, sizeof buffer);
      if (got <= 0) {
        break;
      }
      upstream_bytes += static_cast<size_t>(got);
      if (!status_done) {
        status_line.append(buffer, static_cast<size_t>(got));
        const size_t eol = status_line.find("\r\n");
        if (eol != std::string::npos) {
          status_line.resize(eol);
          status_done = true;
        } else if (status_line.size() > 256) {
          status_line.resize(256);
          status_done = true;
        }
      }
      if (captured.size() >= kMaxInspectBytes) {
        overflow = true;
        break;
      }
      const size_t take = (std::min)(static_cast<size_t>(got),
                                     kMaxInspectBytes - captured.size());
      captured.append(buffer, take);
      if (!framed) {
        head_end = captured.find("\r\n\r\n");
        if (head_end == std::string::npos) {
          continue;
        }
        framed = true;
        chunked = is_chunked(captured, head_end);
        body_expected = chunked ? 0 : content_length(captured, head_end);
      }
      if (chunked) {
        if (captured.find("\r\n0\r\n\r\n", head_end + 4) != std::string::npos ||
            captured.find("0\r\n\r\n", head_end + 4) != std::string::npos) {
          break;
        }
      } else if (captured.size() >= head_end + 4 + body_expected) {
        break;
      }
    }
    std::string output = captured;
    if (inspect && !captured.empty()) {
      const size_t split = captured.find("\r\n\r\n");
      const std::string_view raw_body =
          split == std::string::npos
              ? std::string_view{}
              : std::string_view{captured}.substr(split + 4);
      const auto data = parse_server_data(raw_body);
      if (!data) {
        spx::log::infof("server_data", "no valid server or port field found");
      } else {
        spx::log::infof("server_data", "upstream server=%s port=%u",
                        data->server.c_str(), data->port);
        const std::string comment = first_comment(raw_body);
        if (!comment.empty()) {
          spx::log::infof("server_data", "comment %s", comment.c_str());
        }
        if (self.hooks.on_server_data) {
          self.hooks.on_server_data(data->server, data->port);
        }
      }
      if (rewrite) {
        if (!overflow && split != std::string::npos) {
          const size_t original_length = raw_body.size();
          const std::string body = rewrite_server_data(
              raw_body, self.config.stream_server, self.config.stream_port);
          output =
              rewrite_content_length(captured.substr(0, split), body.size());
          output += "\r\n\r\n";
          output += body;
          if (data) {
            spx::log::infof(
                "server_data", "rewrite %s:%u -> %s:%u length %zu->%zu",
                data->server.c_str(), data->port,
                self.config.stream_server.c_str(), self.config.stream_port,
                original_length, body.size());
          }
        } else {
          spx::log::infof("server_data",
                          "rewrite skipped, relaying captured bytes");
        }
      }
    }
    size_t sent = 0;
    while (sent < output.size()) {
      const int chunk = SSL_write(client, output.data() + sent,
                                  static_cast<int>(output.size() - sent));
      if (chunk <= 0) {
        break;
      }
      sent += static_cast<size_t>(chunk);
    }
    if (status_done) {
      spx::log::infof("https", "upstream response %s", status_line.c_str());
    }
    spx::log::infof("https", "relay done %s %s bytes=%zu",
                    request.method.c_str(), request.path.c_str(),
                    upstream_bytes);
  }
  SSL_shutdown(upstream);
  SSL_free(upstream);
  close_socket(fd);
}

void HttpsService::accept_loop() {
  Impl &self = *impl_;
  for (;;) {
    purge_finished();
    if (!self.running.load()) {
      break;
    }
    SocketHandle handle = accept(self.listen_fd, nullptr, nullptr);
    if (handle == kInvalidSocket) {
      if (!self.running.load()) {
        break;
      }
      continue;
    }
    auto slot = std::make_unique<Slot>();
    slot->thread = std::thread(&HttpsService::connection_thread, this,
                               static_cast<std::uint64_t>(handle), slot.get());
    std::lock_guard<std::mutex> lock(self.mutex);
    self.connections.push_back(std::move(slot));
  }
}

auto HttpsService::start() -> bool {
  Impl &self = *impl_;
  if (self.running.load()) {
    return false;
  }
  std::vector<std::string> names = self.config.hostnames;
  if (names.empty()) {
    names.push_back(self.config.hostname);
  }
  auto pair = detail::generate_certificate_pair(names);
  if (!pair) {
    if (self.hooks.on_warning) {
      self.hooks.on_warning("certificate generation failed");
    }
    return false;
  }

  if (self.config.install_system_trust) {
    platform::remove_stale_ca_certificates("specialproxy CA", {});
    if (platform::install_ca_certificate(pair->ca_der)) {
      self.trust_installed = true;
    } else if (self.hooks.on_warning) {
      self.hooks.on_warning(platform::last_error());
    }
  }

#if defined(_WIN32) || defined(__APPLE__)
  // Hosts redirection stays a caller concern on Linux, Android is not built.
  if (self.config.install_hosts) {
    std::vector<platform::HostEntry> entries;
    for (const auto &name : names) {
      entries.push_back(platform::HostEntry{name, self.config.redirect_ip});
    }
    if (platform::add_host_entries(entries)) {
      self.hosts_installed = true;
    } else if (self.hooks.on_warning) {
      self.hooks.on_warning(platform::last_error());
    }
  }
#endif
  self.ca_der = pair->ca_der;

  SSL_CTX *tls = SSL_CTX_new(TLS_server_method());
  if (tls == nullptr) {
    if (self.hooks.on_warning) {
      self.hooks.on_warning("TLS context creation failed");
    }
    rollback();
    return false;
  }
  SSL_CTX_set_min_proto_version(tls, TLS1_2_VERSION);

  X509Ptr leaf{nullptr};
  PkeyPtr key{nullptr};
  X509Ptr ca{nullptr};
  {
    BIO *leaf_bio = BIO_new_mem_buf(pair->cert_pem.data(),
                                    static_cast<int>(pair->cert_pem.size()));
    leaf.reset(PEM_read_bio_X509(leaf_bio, nullptr, nullptr, nullptr));
    BIO_free(leaf_bio);
    BIO *key_bio = BIO_new_mem_buf(pair->key_pem.data(),
                                   static_cast<int>(pair->key_pem.size()));
    key.reset(PEM_read_bio_PrivateKey(key_bio, nullptr, nullptr, nullptr));
    BIO_free(key_bio);
    BIO *ca_bio = BIO_new_mem_buf(pair->ca_pem.data(),
                                  static_cast<int>(pair->ca_pem.size()));
    ca.reset(PEM_read_bio_X509(ca_bio, nullptr, nullptr, nullptr));
    BIO_free(ca_bio);
  }
  const bool tls_ready = leaf != nullptr && key != nullptr && ca != nullptr &&
                         SSL_CTX_use_certificate(tls, leaf.get()) == 1 &&
                         SSL_CTX_use_PrivateKey(tls, key.get()) == 1 &&
                         SSL_CTX_check_private_key(tls) == 1;
  if (!tls_ready) {
    if (self.hooks.on_warning) {
      self.hooks.on_warning("generated TLS material could not be loaded");
    }
    SSL_CTX_free(tls);
    rollback();
    return false;
  }
  // The context takes ownership of the chain certificate on success.
  if (SSL_CTX_add0_chain_cert(tls, ca.get()) == 1) {
    (void)ca.release();
  }

  SocketHandle handle = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (handle == kInvalidSocket) {
    if (self.hooks.on_warning) {
      self.hooks.on_warning("socket creation failed");
    }
    SSL_CTX_free(tls);
    rollback();
    return false;
  }
  int reuse{1};
  setsockopt(handle, SOL_SOCKET, SO_REUSEADDR,
             reinterpret_cast<const char *>(&reuse), sizeof reuse);
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(self.config.listen_port);
  if (inet_pton(AF_INET, self.config.bind_address.c_str(), &address.sin_addr) !=
      1) {
    address.sin_addr.s_addr = INADDR_ANY;
  }
  if (bind(handle, reinterpret_cast<sockaddr *>(&address), sizeof address) !=
          0 ||
      listen(handle, 16) != 0) {
    if (self.hooks.on_warning) {
      self.hooks.on_warning("bind failed on port " +
                            std::to_string(self.config.listen_port));
    }
    close_socket(handle);
    SSL_CTX_free(tls);
    rollback();
    return false;
  }

  SSL_CTX *upstream{nullptr};
  if (self.config.reverse_proxy) {
    upstream = SSL_CTX_new(TLS_client_method());
    if (upstream == nullptr) {
      if (self.hooks.on_warning) {
        self.hooks.on_warning("upstream TLS context creation failed");
      }
      close_socket(handle);
      SSL_CTX_free(tls);
      rollback();
      return false;
    }
    SSL_CTX_set_verify(upstream, SSL_VERIFY_NONE, nullptr);
  }
  self.tls = tls;
  self.upstream_tls = upstream;
  self.listen_fd = handle;
  self.ca_pem = pair->ca_pem;
  self.running.store(true);
  self.acceptor = std::thread(&HttpsService::accept_loop, this);
  return true;
}

void HttpsService::stop() {
  Impl &self = *impl_;
  const bool was_running = self.running.exchange(false);
  if (was_running) {
    if (self.listen_fd != kInvalidSocket) {
#if defined(_WIN32)
      shutdown(self.listen_fd, SD_BOTH);
#else
      shutdown(self.listen_fd, SHUT_RDWR);
#endif
      close_socket(self.listen_fd);
      self.listen_fd = kInvalidSocket;
    }
    if (self.acceptor.joinable()) {
      self.acceptor.join();
    }
    {
      std::lock_guard<std::mutex> lock(self.mutex);
      for (auto &slot : self.connections) {
        if (slot->thread.joinable()) {
          slot->thread.join();
        }
      }
      self.connections.clear();
    }
  }
  // Installs are undone even when start stopped before the listener came up.
  cleanup_installs();
  if (self.tls != nullptr) {
    SSL_CTX_free(self.tls);
    self.tls = nullptr;
  }
  if (self.upstream_tls != nullptr) {
    SSL_CTX_free(self.upstream_tls);
    self.upstream_tls = nullptr;
  }
}

void HttpsService::rollback() { cleanup_installs(); }

void HttpsService::cleanup_installs() {
  Impl &self = *impl_;
  if (self.hosts_installed) {
    if (!platform::remove_host_entries() && self.hooks.on_warning) {
      self.hooks.on_warning(platform::last_error());
    }
    self.hosts_installed = false;
  }
  if (self.trust_installed) {
    if (platform::uninstall_ca_certificate(self.ca_der)) {
      if (self.hooks.on_ca_removed) {
        self.hooks.on_ca_removed();
      }
    } else if (self.hooks.on_warning) {
      self.hooks.on_warning(platform::last_error());
    }
    self.trust_installed = false;
  }
}

} // namespace spx::https
