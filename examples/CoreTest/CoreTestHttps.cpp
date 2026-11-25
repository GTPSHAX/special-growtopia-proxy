/** @file
 *  @brief Standalone HTTPS bootstrap service for the proxy core.
 *
 *  Runs the HTTPS reverse proxy, rewrites server_data.php so the game client
 *  connects to the UDP proxy, and forwards the advertised upstream to the
 *  core through its loopback control channel. This process never creates a
 *  proxy host and shares no memory with CoreTestProxy.
 */

#include <atomic>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

#include "specialproxy/https/HttpsService.hpp"
#include "specialproxy/net/Net.hpp"
#include "specialproxy/proxy/ControlChannel.hpp"
#include "specialproxy/utils/Logger.hpp"

namespace {

std::atomic_bool g_running{true};

#ifdef _WIN32
BOOL WINAPI handle_ctrl(DWORD type) {
  (void)type;
  g_running = false;
  return TRUE;
}
#endif

/** @brief Parse a decimal port argument with a fallback default. */
auto parse_port(std::string_view text, uint16_t fallback) -> uint16_t {
  uint32_t value{0};
  const auto [end, error] =
      std::from_chars(text.data(), text.data() + text.size(), value);
  if (error != std::errc{} || end != text.data() + text.size() || value < 1 ||
      value > 65535) {
    return fallback;
  }
  return static_cast<uint16_t>(value);
}

} // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
  SetConsoleOutputCP(CP_UTF8);
  SetConsoleCtrlHandler(handle_ctrl, TRUE);
  setvbuf(stdout, nullptr, _IONBF, 0);
#endif

  bool install_components = true;
  std::string socks5_spec;
  std::vector<std::string_view> positional;
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg{argv[i]};
    if (arg == "--no-install") {
      install_components = false;
    } else if (arg == "--socks5") {
      if (i + 1 >= argc) {
        spx::log::error("https", "--socks5 needs a host:port[:user:pass] value");
        return 1;
      }
      socks5_spec = argv[++i];
    } else if (arg.starts_with("--socks5=")) {
      socks5_spec = arg.substr(9);
    } else if (!arg.starts_with("--")) {
      positional.push_back(arg);
    }
  }
  const auto positional_value = [&positional](size_t index,
                                              std::string_view fallback) {
    return index < positional.size() ? positional[index] : fallback;
  };
  const uint16_t proxy_port =
      parse_port(positional_value(0, "17099"), 17099);
  const uint16_t control_port =
      parse_port(positional_value(1, "17098"), 17098);
  const uint16_t https_port = parse_port(positional_value(2, "443"), 443);
  spx::net::Socks5Config socks5;
  if (!socks5_spec.empty() &&
      !spx::net::socks5_parse(socks5_spec, socks5)) {
    spx::log::error("https", "invalid socks5 specification");
    return 1;
  }

  spx::log::infof("https",
                  "bootstrap service starting listen_tcp=%u proxy_udp=%u "
                  "control=%u install=%d",
                  https_port, proxy_port, control_port,
                  install_components ? 1 : 0);
  if (!spx::net::initialize()) {
    spx::log::error("https", "socket initialization failed");
    return 1;
  }

  spx::https::HttpsConfig config;
  config.listen_port = https_port;
  config.install_system_trust = install_components;
  config.install_hosts = install_components;
  config.rewrite_server_data = true;
  config.stream_server = "127.0.0.1";
  config.stream_port = proxy_port;
  config.socks5 = socks5;

  spx::https::HttpsHooks hooks;
  hooks.on_warning = [](std::string_view message) {
    spx::log::warningf("https", "%.*s", static_cast<int>(message.size()),
                       message.data());
  };
  hooks.on_server_data = [control_port](std::string_view server,
                                        uint16_t port) {
    spx::log::infof("https", "server_data upstream %.*s:%u",
                    static_cast<int>(server.size()), server.data(), port);
    if (!spx::proxy::send_control_upstream(control_port, server, port)) {
      spx::log::warningf("https",
                         "failed to send upstream to control 127.0.0.1:%u",
                         control_port);
      return;
    }
    spx::log::infof("https", "upstream sent to control 127.0.0.1:%u",
                    control_port);
  };

  if (!spx::https::https_start(std::move(config), std::move(hooks))) {
    spx::log::error("https", "service failed to start");
    spx::net::deinitialize();
    return 1;
  }
  const auto active = spx::https::https_socks5();
  spx::log::infof("https",
                  "bootstrap service ready socks5=%d host=%s port=%u auth=%d",
                  active.enabled ? 1 : 0, active.host.c_str(), active.port,
                  active.has_auth ? 1 : 0);

  while (g_running) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }
  spx::https::https_stop();
  spx::net::deinitialize();
  spx::log::infof("https", "bootstrap service stopped");
  return 0;
}
