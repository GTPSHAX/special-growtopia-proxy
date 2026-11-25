/** @file
 *  @brief Core public entry point and exported control functions.
 */

#ifndef SPXCORE_PROXY_CORE_HPP
#define SPXCORE_PROXY_CORE_HPP

#include <cstdint>
#include <string>
#include <string_view>

#include "specialproxy/net/Socks5.hpp"
#include "specialproxy/platforms/Platform.hpp"
#include "specialproxy/proxy/ProxyConfig.hpp"

namespace spx {

/**
 * @brief Get the library version string.
 *
 * @return Version in "major.minor.patch" form.
 */
auto version() noexcept -> std::string_view;

/**
 * @brief Start the Growtopia proxy entry point.
 *
 * Creates the client host and services sessions on a background thread.
 * Redirect packets of type OnSendToServer are intercepted and rewritten
 * toward the proxy. The config is copied, later
 * changes through the exported toggles only. The initial target comes from
 * the config, or from a value previously supplied through proxy_set_upstream.
 *
 * @param config ProxyConfig with ports, target server and session limits.
 * @return True when the proxy is running, false when hosts could not be
 *         created or the proxy is already running.
 */
auto proxy_start(const proxy::ProxyConfig& config) -> bool;

/**
 * @brief Stop the proxy entry point and release all hosts.
 *
 * Safe to call without a running proxy.
 */
void proxy_stop();

/**
 * @brief Toggle a named proxy feature at runtime.
 *
 * Known features are "intercept_redirect" (rewrite OnSendToServer, default
 * on) and "verbose" (log every forwarded packet to stdout, default off).
 * Unknown features are ignored.
 *
 * @param feature Feature name.
 * @param enabled True to enable the feature.
 * @return True when the feature name was known.
 */
auto proxy_toggle(std::string_view feature, bool enabled) -> bool;

/**
 * @brief Send a console message to every connected client.
 *
 * @param message Text carried by the OnConsoleMessage variant.
 * @return True when the proxy is running.
 */
auto proxy_message_clients(std::string_view message) -> bool;

/**
 * @brief Supply the upstream game server endpoint from an external source.
 *
 * Discovery and relay services call this to retarget sessions created after
 * the call. Existing sessions keep their current target. The value is also
 * returned by proxy_upstream. Calling this before proxy_start overrides the
 * target carried by the ProxyConfig passed to proxy_start.
 *
 * @param[in] server Address of the real game server.
 * @param[in] port UDP port of the real game server.
 * @return True when the endpoint was recorded.
 */
auto proxy_set_upstream(std::string_view server, uint16_t port) -> bool;

/** Upstream endpoint supplied through proxy_set_upstream. */
struct UpstreamInfo {
  std::string server; ///< Supplied server address, empty before a call.
  uint16_t port{0};   ///< Supplied game port, zero before a call.
};

/**
 * @brief Read the upstream endpoint recorded by proxy_set_upstream.
 *
 * @return Last supplied address and port, or empty values when no endpoint
 *         has been supplied yet.
 */
auto proxy_upstream() -> UpstreamInfo;

/** Secret-free view of the active SOCKS5 selection. */
struct Socks5Status {
  bool enabled{false}; ///< Whether upstream traffic uses the proxy.
  std::string host;    ///< Proxy host, empty when disabled.
  uint16_t port{0};    ///< Proxy port, zero when disabled.
  bool has_auth{false};///< Whether credentials are configured.
};

/**
 * @brief Replace the SOCKS5 selection at runtime.
 *
 * Existing sessions keep their current relay; new sessions use the supplied
 * settings. Password is never logged.
 *
 * @param[in] config Proxy settings, disabled when config.enabled is false.
 * @return True when the settings are valid and were recorded.
 */
auto proxy_set_socks5(const net::Socks5Config& config) -> bool;

/**
 * @brief Read the active SOCKS5 selection.
 *
 * @return Status without credentials.
 */
auto proxy_socks5() -> Socks5Status;

/**
 * @brief Read descriptive information about the machine running the core.
 *
 * @return Host name, operating system, platform, architecture and CPU count.
 */
auto device_info() -> platform::DeviceInfo;

} // namespace

#endif // SPXCORE_PROXY_CORE_HPP
