/** @file
 *  @brief TLS roundtrip through the HTTPS service with its own CA.
 */

#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <future>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include <openssl/pem.h>
#include <openssl/ssl.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

#include "PlatformError.hpp"
#include "net/Socks5Tcp.hpp"
#include "proxy/https/CertificateFactory.hpp"
#include "proxy/https/DnsResolver.hpp"
#include "proxy/https/HttpsService.hpp"
#include "proxy/https/ServerData.hpp"
#include "specialproxy/net/Net.hpp"

#if defined(_WIN32)
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace {

using spx::https::HttpRequest;
using spx::https::HttpResponse;
using spx::https::HttpsConfig;
using spx::https::HttpsHooks;
using spx::https::HttpsService;

/** @brief Parse the CA PEM and check the subject common name. */
auto check_ca_subject(const std::string &ca_pem) -> void {
  BIO *bio = BIO_new_mem_buf(ca_pem.data(), static_cast<int>(ca_pem.size()));
  assert(bio != nullptr);
  X509 *ca = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr);
  BIO_free(bio);
  assert(ca != nullptr);
  char buffer[64]{};
  const int length = X509_NAME_get_text_by_NID(
      X509_get_subject_name(ca), NID_commonName, buffer, sizeof buffer);
  assert(length > 0);
  assert(std::string{buffer} == "specialproxy CA");
  X509_free(ca);
}

/** @brief TCP connect to the local listener. */
auto connect_local(uint16_t port) -> int {
  int fd = static_cast<int>(socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
  assert(fd >= 0);
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(port);
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  assert(connect(fd, reinterpret_cast<sockaddr *>(&address), sizeof address) ==
         0);
  return fd;
}

/** @brief Generated leaf carries an Authority Key Identifier and verifies. */
auto test_generated_pair() -> void {
  const std::vector<std::string> names{"www.growtopia1.com",
                                       "login.growtopiagame.com"};
  auto pair = spx::https::detail::generate_certificate_pair(names);
  assert(pair.has_value());

  BIO *ca_bio = BIO_new_mem_buf(pair->ca_pem.data(),
                                static_cast<int>(pair->ca_pem.size()));
  X509 *ca = PEM_read_bio_X509(ca_bio, nullptr, nullptr, nullptr);
  BIO_free(ca_bio);
  BIO *leaf_bio = BIO_new_mem_buf(pair->cert_pem.data(),
                                  static_cast<int>(pair->cert_pem.size()));
  X509 *leaf = PEM_read_bio_X509(leaf_bio, nullptr, nullptr, nullptr);
  BIO_free(leaf_bio);
  assert(ca != nullptr && leaf != nullptr);
  assert(X509_get_ext_d2i(leaf, NID_authority_key_identifier, nullptr,
                          nullptr) != nullptr);
  for (const auto &name : names) {
    assert(X509_check_host(leaf, name.data(), name.size(), 0, nullptr) == 1);
  }

  X509_STORE *store = X509_STORE_new();
  assert(store != nullptr);
  assert(X509_STORE_add_cert(store, ca) == 1);
  X509_STORE_CTX *context = X509_STORE_CTX_new();
  assert(context != nullptr);
  assert(X509_STORE_CTX_init(context, store, leaf, nullptr) == 1);
  assert(X509_verify_cert(context) == 1);
  X509_STORE_CTX_free(context);
  X509_STORE_free(store);
  X509_free(leaf);
  X509_free(ca);
}

/** @brief Stopping the service restores the hosts file it modified. */
auto test_hosts_cleanup_on_stop() -> void {
#if defined(_WIN32) || defined(__APPLE__)
  const auto directory =
      std::filesystem::temp_directory_path() / "specialproxy-https-hosts";
  std::filesystem::create_directories(directory);
  const auto path = directory / "hosts";
  const std::string sample =
      "202.155.143.176 www.growtopia1.com # Lowping Lucky\n"
      "127.0.0.1 localhost\n";
  {
    std::ofstream out{path, std::ios::binary | std::ios::trunc};
    out << sample;
  }
  spx::platform::detail::set_hosts_path_override(path.string());

  HttpsConfig config;
  config.listen_port = 18444;
  config.install_system_trust = false;
  config.install_hosts = true;
  HttpsService service{config, HttpsHooks{}};
  assert(service.start());

  auto read = [&] {
    std::ifstream in{path, std::ios::binary};
    std::ostringstream stream;
    stream << in.rdbuf();
    return stream.str();
  };
  assert(read().find("127.0.0.1 www.growtopia1.com") != std::string::npos);
  service.stop();
  const std::string restored = read();
  assert(restored.find("127.0.0.1 www.growtopia1.com") == std::string::npos);
  assert(restored.find("# spx-disabled") == std::string::npos);
  assert(restored.find("202.155.143.176 www.growtopia1.com # Lowping "
                       "Lucky") != std::string::npos);

  spx::platform::detail::set_hosts_path_override({});
  std::remove(path.string().c_str());
#endif
}

/** @brief Full HTTPS GET through the generated certificate chain. */
auto test_tls_roundtrip() -> void {
  HttpsConfig config;
  config.listen_port = 18443;
  config.install_system_trust = false;
  config.install_hosts = false;
  config.reverse_proxy = false;
  std::string warning;
  HttpsHooks hooks;
  hooks.on_request = [](const HttpRequest &request) {
    HttpResponse response;
    response.body = "handler " + request.path;
    return response;
  };
  hooks.on_warning = [&](std::string_view message) { warning = message; };

  HttpsService service{config, std::move(hooks)};
  assert(service.start());
  assert(service.running());
  const std::string ca_pem = service.ca_pem();
  assert(!ca_pem.empty());
  check_ca_subject(ca_pem);

  BIO *bio = BIO_new_mem_buf(ca_pem.data(), static_cast<int>(ca_pem.size()));
  X509 *ca = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr);
  BIO_free(bio);
  assert(ca != nullptr);

  SSL_CTX *client = SSL_CTX_new(TLS_client_method());
  assert(client != nullptr);
  assert(X509_STORE_add_cert(SSL_CTX_get_cert_store(client), ca) == 1);
  SSL *ssl = SSL_new(client);
  assert(ssl != nullptr);
  const int fd = connect_local(config.listen_port);
  assert(SSL_set_fd(ssl, fd) == 1);
  const std::string host{"www.growtopia1.com"};
  SSL_set_tlsext_host_name(ssl, host.c_str());
  assert(SSL_set1_host(ssl, host.c_str()) == 1);
  assert(SSL_connect(ssl) == 1);

  const std::string request = "GET /dashboard HTTP/1.1\r\nHost: " + host +
                              "\r\nConnection: close\r\n\r\n";
  assert(SSL_write(ssl, request.data(), static_cast<int>(request.size())) ==
         static_cast<int>(request.size()));
  std::string response;
  char buffer[2048];
  for (;;) {
    const int got = SSL_read(ssl, buffer, sizeof buffer);
    if (got <= 0) {
      break;
    }
    response.append(buffer, static_cast<size_t>(got));
  }
  assert(response.starts_with("HTTP/1.1 200"));
  assert(response.find("handler /dashboard") != std::string::npos);

  SSL_shutdown(ssl);
  SSL_free(ssl);
  SSL_CTX_free(client);
  X509_free(ca);
#if defined(_WIN32)
  closesocket(fd);
#else
  ::close(fd);
#endif
  service.stop();
  assert(!service.running());
  assert(warning.empty());
}

/** @brief Parse a synthetic A record with a compressed answer name. */
auto test_dns_parse() -> void {
  std::vector<uint8_t> response{0x12, 0x34, 0x81, 0x80, 0x00, 0x01,
                                0x00, 0x01, 0x00, 0x00, 0x00, 0x00};
  const char encoded_name[] = {4,   't', 'e', 's', 't', 7,   'e',
                               'x', 'a', 'm', 'p', 'l', 'e', 0};
  for (char value : encoded_name) {
    response.push_back(static_cast<uint8_t>(value));
  }
  response.push_back(0);
  response.push_back(1);
  response.push_back(0);
  response.push_back(1);
  response.push_back(0xC0);
  response.push_back(0x0C);
  response.push_back(0);
  response.push_back(1);
  response.push_back(0);
  response.push_back(1);
  response.push_back(0);
  response.push_back(0);
  response.push_back(0);
  response.push_back(60);
  response.push_back(0);
  response.push_back(4);
  response.push_back(1);
  response.push_back(2);
  response.push_back(3);
  response.push_back(4);

  const auto address =
      spx::https::dns::detail::parse_response(response, 0x1234);
  assert(address && *address == "1.2.3.4");
  assert(!spx::https::dns::detail::parse_response(response, 0x9999));
}

/** @brief Query a public resolver for the real upstream address. */
auto test_dns_live() -> void {
  const std::vector<std::string> servers{"1.1.1.1", "8.8.8.8"};
  const auto address =
      spx::https::dns::resolve("www.growtopia1.com", servers, 2000);
  if (address) {
    std::printf("[dns] www.growtopia1.com -> %s\n", address->c_str());
    assert(*address != "127.0.0.1");
    unsigned first{0};
    unsigned second{0};
    unsigned third{0};
    unsigned fourth{0};
    assert(std::sscanf(address->c_str(), "%u.%u.%u.%u", &first, &second, &third,
                       &fourth) == 4);
  } else {
    std::printf("[dns] public lookup unavailable, skipped\n");
  }
}

/** @brief Parse the pipe delimited discovery body. */
auto test_server_data_parse() -> void {
  const std::string body =
      "server|213.179.209.175\nport|17045\nloginurl|login.growtopiagame.com\n";
  const auto parsed = spx::https::parse_server_data(body);
  assert(parsed);
  assert(parsed->server == "213.179.209.175");
  assert(parsed->port == 17045);

  const std::string crlf = "server|1.2.3.4\r\nport|5555\r\n";
  const auto windows = spx::https::parse_server_data(crlf);
  assert(windows);
  assert(windows->server == "1.2.3.4");
  assert(windows->port == 5555);

  assert(!spx::https::parse_server_data("port|17045\n"));
  assert(!spx::https::parse_server_data("server|1.2.3.4\n"));
  assert(!spx::https::parse_server_data("server|1.2.3.4\nport|abc\n"));
  assert(!spx::https::parse_server_data("server|1.2.3.4\nport|70000\n"));
  assert(!spx::https::parse_server_data("server|1.2.3.4\nport|17045x\n"));
}

/** @brief Rewriting only touches the discovery fields and keeps line endings.
 */
auto test_rewrite_server_data() -> void {
  const std::string body = "server|213.179.209.175\nport|17045\r\nloginurl|"
                           "login.growtopiagame.com\n";
  const std::string rewritten =
      spx::https::rewrite_server_data(body, "127.0.0.1", 17099);
  assert(rewritten.find("server|127.0.0.1\n") == 0);
  assert(rewritten.find("port|17099\r\n") != std::string::npos);
  assert(rewritten.find("loginurl|login.growtopiagame.com\n") !=
         std::string::npos);
  assert(rewritten.find("213.179.209.175") == std::string::npos);
  assert(rewritten.find("17045") == std::string::npos);
}

/** @brief Local TLS origin that answers one request for the proxy test. */
auto run_fake_upstream(uint16_t port, std::promise<void> ready,
                       const spx::https::detail::CertificatePair &pair,
                       std::string reply_body) -> void {
  SSL_CTX *context = SSL_CTX_new(TLS_server_method());
  assert(context != nullptr);
  BIO *cert_bio = BIO_new_mem_buf(pair.cert_pem.data(),
                                  static_cast<int>(pair.cert_pem.size()));
  X509 *cert = PEM_read_bio_X509(cert_bio, nullptr, nullptr, nullptr);
  BIO_free(cert_bio);
  BIO *key_bio = BIO_new_mem_buf(pair.key_pem.data(),
                                 static_cast<int>(pair.key_pem.size()));
  EVP_PKEY *key = PEM_read_bio_PrivateKey(key_bio, nullptr, nullptr, nullptr);
  BIO_free(key_bio);
  assert(cert != nullptr && key != nullptr);
  assert(SSL_CTX_use_certificate(context, cert) == 1);
  assert(SSL_CTX_use_PrivateKey(context, key) == 1);
  X509_free(cert);
  EVP_PKEY_free(key);

  const int listener =
      static_cast<int>(socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
  assert(listener >= 0);
  int reuse{1};
  setsockopt(listener, SOL_SOCKET, SO_REUSEADDR,
             reinterpret_cast<const char *>(&reuse), sizeof reuse);
  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(port);
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  assert(bind(listener, reinterpret_cast<sockaddr *>(&address),
              sizeof address) == 0);
  assert(listen(listener, 1) == 0);
  ready.set_value();

  const int fd = static_cast<int>(accept(listener, nullptr, nullptr));
  assert(fd >= 0);
  SSL *ssl = SSL_new(context);
  assert(ssl != nullptr);
  assert(SSL_set_fd(ssl, fd) == 1);
  assert(SSL_accept(ssl) == 1);
  char buffer[4096];
  (void)SSL_read(ssl, buffer, sizeof buffer);
  const std::string head =
      "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: " +
      std::to_string(reply_body.size()) + "\r\nConnection: close\r\n\r\n";
  const std::string reply = head + reply_body;
  assert(SSL_write(ssl, reply.data(), static_cast<int>(reply.size())) ==
         static_cast<int>(reply.size()));
  SSL_shutdown(ssl);
  SSL_free(ssl);
#if defined(_WIN32)
  closesocket(fd);
  closesocket(listener);
#else
  ::close(fd);
  ::close(listener);
#endif
  SSL_CTX_free(context);
}

/** @brief Requests are relayed to the upstream and the reply reaches the
 * client. */
auto test_reverse_proxy() -> void {
  auto origin = spx::https::detail::generate_certificate_pair(
      std::vector<std::string>{"www.growtopia1.com"});
  assert(origin);
  const uint16_t origin_port{18445};
  const uint16_t listen_port{18446};
  std::promise<void> ready;
  auto listening = ready.get_future();
  std::thread upstream{run_fake_upstream, origin_port, std::move(ready),
                       *origin, std::string{"upstream ok"}};
  listening.wait();

  HttpsConfig config;
  config.listen_port = listen_port;
  config.install_system_trust = false;
  config.install_hosts = false;
  config.upstream_address_override = "127.0.0.1";
  config.upstream_port = origin_port;
  HttpsService service{config, HttpsHooks{}};
  assert(service.start());

  SSL_CTX *client = SSL_CTX_new(TLS_client_method());
  assert(client != nullptr);
  const std::string ca = service.ca_pem();
  BIO *bio = BIO_new_mem_buf(ca.data(), static_cast<int>(ca.size()));
  X509 *ca_cert = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr);
  BIO_free(bio);
  assert(ca_cert != nullptr);
  assert(X509_STORE_add_cert(SSL_CTX_get_cert_store(client), ca_cert) == 1);
  SSL *ssl = SSL_new(client);
  assert(ssl != nullptr);
  const int fd = connect_local(listen_port);
  assert(SSL_set_fd(ssl, fd) == 1);
  const std::string host{"www.growtopia1.com"};
  SSL_set_tlsext_host_name(ssl, host.c_str());
  assert(SSL_set1_host(ssl, host.c_str()) == 1);
  assert(SSL_connect(ssl) == 1);

  const std::string request = "GET /game/login.php HTTP/1.1\r\nHost: " + host +
                              "\r\nConnection: close\r\n\r\n";
  assert(SSL_write(ssl, request.data(), static_cast<int>(request.size())) ==
         static_cast<int>(request.size()));
  std::string response;
  char buffer[2048];
  for (;;) {
    const int got = SSL_read(ssl, buffer, sizeof buffer);
    if (got <= 0) {
      break;
    }
    response.append(buffer, static_cast<size_t>(got));
  }
  assert(response.starts_with("HTTP/1.1 200"));
  assert(response.find("upstream ok") != std::string::npos);

  SSL_shutdown(ssl);
  SSL_free(ssl);
  SSL_CTX_free(client);
  X509_free(ca_cert);
#if defined(_WIN32)
  closesocket(fd);
#else
  ::close(fd);
#endif
  service.stop();
  upstream.join();
}

/** @brief Intercepting server_data.php records the advertised upstream. */
auto test_server_data_intercept() -> void {
  auto origin = spx::https::detail::generate_certificate_pair(
      std::vector<std::string>{"www.growtopia1.com"});
  assert(origin);
  const std::string body =
      "server|213.179.209.175\nport|17045\nloginurl|login.growtopiagame.com\n";
  const uint16_t origin_port{18447};
  const uint16_t listen_port{18448};
  std::promise<void> ready;
  auto listening = ready.get_future();
  std::thread upstream{run_fake_upstream, origin_port, std::move(ready),
                       *origin, body};
  listening.wait();

  std::string recorded_server;
  uint16_t recorded_port{0};
  HttpsConfig config;
  config.listen_port = listen_port;
  config.install_system_trust = false;
  config.install_hosts = false;
  config.upstream_address_override = "127.0.0.1";
  config.upstream_port = origin_port;
  config.stream_server = "127.0.0.1";
  config.stream_port = 17099;
  HttpsHooks hooks;
  hooks.on_server_data = [&](std::string_view server, uint16_t port) {
    recorded_server.assign(server);
    recorded_port = port;
  };
  HttpsService service{config, std::move(hooks)};
  assert(service.start());

  SSL_CTX *client = SSL_CTX_new(TLS_client_method());
  assert(client != nullptr);
  const std::string ca = service.ca_pem();
  BIO *bio = BIO_new_mem_buf(ca.data(), static_cast<int>(ca.size()));
  X509 *ca_cert = PEM_read_bio_X509(bio, nullptr, nullptr, nullptr);
  BIO_free(bio);
  assert(ca_cert != nullptr);
  assert(X509_STORE_add_cert(SSL_CTX_get_cert_store(client), ca_cert) == 1);
  SSL *ssl = SSL_new(client);
  assert(ssl != nullptr);
  const int fd = connect_local(listen_port);
  assert(SSL_set_fd(ssl, fd) == 1);
  const std::string host{"www.growtopia1.com"};
  SSL_set_tlsext_host_name(ssl, host.c_str());
  assert(SSL_set1_host(ssl, host.c_str()) == 1);
  assert(SSL_connect(ssl) == 1);

  const std::string request =
      "POST /growtopia/server_data.php HTTP/1.1\r\nHost: " + host +
      "\r\nContent-Length: 0\r\nConnection: close\r\n\r\n";
  assert(SSL_write(ssl, request.data(), static_cast<int>(request.size())) ==
         static_cast<int>(request.size()));
  std::string response;
  char buffer[2048];
  for (;;) {
    const int got = SSL_read(ssl, buffer, sizeof buffer);
    if (got <= 0) {
      break;
    }
    response.append(buffer, static_cast<size_t>(got));
  }
  assert(response.find("server|127.0.0.1") != std::string::npos);
  assert(response.find("port|17099") != std::string::npos);
  assert(response.find("213.179.209.175") == std::string::npos);
  assert(response.find("17045") == std::string::npos);
  const size_t head_end = response.find("\r\n\r\n");
  assert(head_end != std::string::npos);
  const std::string declared =
      "Content-Length: " + std::to_string(response.size() - head_end - 4);
  assert(response.find(declared) != std::string::npos);
  assert(recorded_server == "213.179.209.175");
  assert(recorded_port == 17045);

  SSL_shutdown(ssl);
  SSL_free(ssl);
  SSL_CTX_free(client);
  X509_free(ca_cert);
#if defined(_WIN32)
  closesocket(fd);
#else
  ::close(fd);
#endif
  service.stop();
  upstream.join();
}

} // namespace

auto test_socks5_api() -> void {
  const auto request =
      spx::net::socks5_tcp::build_connect_request("www.growtopia1.com", 443);
  assert(request.size() == 4u + 1u + 18u + 2u);
  assert(request[0] == 0x05 && request[1] == 0x01 && request[2] == 0x00 &&
         request[3] == 0x03);
  assert(request[4] == 18);
  assert(request[request.size() - 2] == 0x01);
  assert(request[request.size() - 1] == 0xBB);

  assert(!spx::https::https_running());
  assert(!spx::https::https_set_socks5({}));
  const auto status = spx::https::https_socks5();
  assert(!status.enabled);
  assert(status.host.empty());
  assert(status.port == 0);
  assert(!status.has_auth);
}

int main() {
  assert(spx::net::initialize());
  test_socks5_api();
  test_generated_pair();
  test_dns_parse();
  test_dns_live();
  test_server_data_parse();
  test_rewrite_server_data();
  test_reverse_proxy();
  test_server_data_intercept();
  test_hosts_cleanup_on_stop();
  test_tls_roundtrip();
  spx::net::deinitialize();
  return 0;
}
