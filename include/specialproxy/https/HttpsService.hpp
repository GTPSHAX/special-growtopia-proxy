/** @file
 *  @brief HTTPS service with generated TLS certificates and hosts setup.
 */

#ifndef SPXCORE_HTTPS_HTTPS_SERVICE_HPP
#define SPXCORE_HTTPS_HTTPS_SERVICE_HPP

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

#include "specialproxy/net/Socks5.hpp"

struct ssl_st;

namespace spx::https {

/** Request line and body received from a client. */
struct HttpRequest {
  std::string method; ///< Verb such as GET or POST.
  std::string path;   ///< Request target without the query part.
  std::string query;  ///< Raw query string without the question mark.
  std::string body;   ///< Request body bytes.
  std::string host;   ///< Value of the Host header, empty when absent.
  std::string raw;    ///< Complete request bytes as received.
};

/** Reply the service sends back over the TLS connection. */
struct HttpResponse {
  unsigned status{200};                   ///< HTTP status code.
  std::string content_type{"text/plain"}; ///< Media type of the body.
  std::string body;                       ///< Response body bytes.
};

/** Optional callbacks around request handling and install problems. */
struct HttpsHooks {
  /** Invoked for every request, a null hook answers 404. */
  std::function<HttpResponse(const HttpRequest&)> on_request;
  /** Invoked with non fatal install problems. */
  std::function<void(std::string_view)> on_warning;
  /** Invoked after the generated CA is removed from the trust store. */
  std::function<void()> on_ca_removed;
  /** Invoked with the upstream advertised by a server_data.php response. */
  std::function<void(std::string_view server, uint16_t port)> on_server_data;
};

/** Settings for the listener, the certificate and the system installs. */
struct HttpsConfig {
  uint16_t listen_port{443}; ///< TCP port of the HTTPS listener.
  std::string hostname{"login.growtopiagame.com"}; ///< Name in the leaf certificate SAN.
  std::string bind_address{"0.0.0.0"};             ///< Listener bind address.
  std::string redirect_ip{"127.0.0.1"};            ///< Address written into the hosts file.
  std::vector<std::string> hostnames{              ///< Names redirected by the hosts file.
    "www.growtopia1.com", "login.growtopiagame.com"};
  bool install_system_trust{true};                 ///< Install the generated CA into the trust store.
  bool install_hosts{true};                        ///< Point the hostname at redirect_ip.
  bool reverse_proxy{true};                        ///< Forward requests to the real upstream host.
  std::string upstream_host{"www.growtopia1.com"}; ///< Name resolved and used as TLS SNI.
  uint16_t upstream_port{443};                     ///< TCP port of the upstream HTTPS server.
  std::string upstream_address_override;           ///< Skip DNS and connect to this address.
  std::vector<std::string> dns_servers{            ///< Recursive DNS servers tried in order.
    "1.1.1.1", "8.8.8.8"};
  int dns_timeout_ms{2000}; ///< Socket timeout for each DNS server, in milliseconds.
  bool rewrite_server_data{true};             ///< Point discovery fields at the stream proxy.
  std::string stream_server{"127.0.0.1"};     ///< Address written into the server field.
  uint16_t stream_port{0};                    ///< Port written into the port field.
  net::Socks5Config socks5;                   ///< Proxy for upstream HTTPS traffic.
};

/** HTTPS listener that serves a self generated certificate. */
class HttpsService {
 public:
  /**
   * @brief Construct the service without starting it.
   *
   * @param config Listener and install settings.
   * @param hooks Optional request and warning callbacks.
   */
  HttpsService(HttpsConfig config, HttpsHooks hooks);
  /** @brief Stop the service and release the TLS context. */
  ~HttpsService();
  HttpsService(const HttpsService&) = delete;
  auto operator=(const HttpsService&) -> HttpsService& = delete;

  /**
   * @brief Generate certificates, install trust and hosts, then listen.
   *
   * A failing CA or hosts install only warns. A failing certificate
   * generation or listener bind stops the start. On Windows the caller
   * must have initialized Winsock first, the proxy facade does this
   * through its net initialization.
   *
   * @return True when the listener is running.
   */
  auto start() -> bool;

  /** @brief Close the listener, join connection threads and undo hosts. */
  void stop();

  /**
   * @brief Listener state.
   *
   * @return True between a successful start and stop.
   */
  [[nodiscard]] auto running() const noexcept -> bool;

  /**
   * @brief PEM body of the generated CA certificate.
   *
   * @return Empty before a successful start.
   */
  auto ca_pem() const -> std::string;

  /**
   * @brief Replace the SOCKS5 selection for future upstream requests.
   *
   * @param[in] config Proxy settings, disabled when config.enabled is false.
   */
  void set_socks5(const net::Socks5Config& config);

 private:
  struct Impl;
  struct Slot;
  std::unique_ptr<Impl> impl_;

  void accept_loop();
  void purge_finished();
  void connection_thread(std::uint64_t handle, Slot* slot);
  void relay_to_upstream(ssl_st* client, const HttpRequest& request);
  void rollback();
  void cleanup_installs();
};

/** Secret-free view of the active HTTPS SOCKS5 selection. */
struct HttpsSocks5Status {
  bool enabled{false};  ///< Whether upstream traffic uses the proxy.
  std::string host;     ///< Proxy host, empty when disabled.
  uint16_t port{0};     ///< Proxy port, zero when disabled.
  bool has_auth{false}; ///< Whether credentials are configured.
};

/**
 * @brief Start the process wide HTTPS bootstrap service.
 *
 * @param[in] config Listener and install settings.
 * @param[in] hooks Optional request and warning callbacks.
 * @return True when the listener is running.
 */
auto https_start(HttpsConfig config, HttpsHooks hooks = {}) -> bool;

/** @brief Stop the process wide HTTPS bootstrap service. */
void https_stop();

/**
 * @brief Read the listener state.
 *
 * @return True while the process wide service is running.
 */
auto https_running() -> bool;

/**
 * @brief Replace SOCKS5 settings on the running HTTPS service.
 *
 * @param[in] config Proxy settings, disabled when config.enabled is false.
 * @return True when a service is running and the settings are valid.
 */
auto https_set_socks5(const net::Socks5Config& config) -> bool;

/**
 * @brief Read the active HTTPS SOCKS5 selection.
 *
 * @return Status without credentials.
 */
auto https_socks5() -> HttpsSocks5Status;

} // namespace spx::https

#endif // SPXCORE_HTTPS_HTTPS_SERVICE_HPP
