/** @file
 *  @brief Combined Windows proxy and HTTPS bootstrap distribution app.
 */

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

#include "specialproxy/https/HttpsService.hpp"
#include "specialproxy/net/Socks5.hpp"
#include "specialproxy/proxy/ControlChannel.hpp"
#include "specialproxy/proxy/Core.hpp"
#include "specialproxy/script/Script.hpp"
#include "specialproxy/utils/Logger.hpp"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace {

std::atomic_bool g_running{true};

BOOL WINAPI handle_console_event(DWORD type) {
  switch (type) {
  case CTRL_C_EVENT:
  case CTRL_BREAK_EVENT:
  case CTRL_CLOSE_EVENT:
  case CTRL_SHUTDOWN_EVENT:
    g_running.store(false, std::memory_order_release);
    return TRUE;
  default:
    return FALSE;
  }
}

auto parse_arguments(int argc, char **argv, std::string &upstream,
                     spx::net::Socks5Config &socks5, bool &show_help) -> bool {
  upstream = "www.growtopia1.com";
  show_help = false;
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg{argv[i]};
    if (arg == "--help" || arg == "-h") {
      show_help = true;
      return false;
    }
    if (arg == "--socks5") {
      if (i + 1 >= argc || !spx::net::socks5_parse(argv[++i], socks5)) {
        spx::log::error("app", "invalid or missing SOCKS5 specification");
        return false;
      }
    } else if (arg.starts_with("--socks5=")) {
      if (!spx::net::socks5_parse(arg.substr(9), socks5)) {
        spx::log::error("app", "invalid SOCKS5 specification");
        return false;
      }
    } else if (arg.starts_with("--")) {
      spx::log::errorf("app", "unknown option: %.*s",
                       static_cast<int>(arg.size()), arg.data());
      return false;
    } else {
      upstream.assign(arg);
    }
  }
  return true;
}

} // namespace

int main(int argc, char **argv) {
  SetConsoleOutputCP(CP_UTF8);
  SetConsoleCtrlHandler(handle_console_event, TRUE);
  setvbuf(stdout, nullptr, _IONBF, 0);

  std::string upstream;
  spx::net::Socks5Config socks5;
  bool show_help{false};
  if (!parse_arguments(argc, argv, upstream, socks5, show_help)) {
    if (show_help) {
      std::printf("Usage: SpecialProxy.exe [upstream-host] "
                  "[--socks5 host:port[:user:pass]]\n");
      return 0;
    }
    return 2;
  }

  spx::script_set_directory("bin/scripts");

  spx::proxy::ProxyConfig proxy_config;
  proxy_config.listen_port = 17099;
  proxy_config.server_address = upstream;
  proxy_config.server_port = 17091;
  proxy_config.redirect_address = "127.0.0.1";
  proxy_config.control_port = 17098;
  proxy_config.socks5 = socks5;

  spx::log::infof("app", "SpecialProxy %.*s starting upstream=%s",
                  static_cast<int>(spx::version().size()),
                  spx::version().data(), upstream.c_str());
  if (!spx::proxy_start(proxy_config)) {
    spx::log::error("app", "proxy failed to start");
    return 1;
  }

  spx::https::HttpsConfig https_config;
  https_config.listen_port = 443;
  https_config.install_system_trust = true;
  https_config.install_hosts = true;
  https_config.rewrite_server_data = true;
  https_config.stream_server = "127.0.0.1";
  https_config.stream_port = proxy_config.listen_port;
  https_config.socks5 = socks5;

  spx::https::HttpsHooks hooks;
  hooks.on_warning = [](std::string_view message) {
    spx::log::warningf("https", "%.*s", static_cast<int>(message.size()),
                       message.data());
  };
  hooks.on_server_data = [](std::string_view server, uint16_t port) {
    spx::log::infof("https", "discovered upstream=%.*s:%u",
                    static_cast<int>(server.size()), server.data(), port);
    if (!spx::proxy::send_control_upstream(17098, server, port)) {
      spx::log::warning("https", "failed to update proxy upstream");
    }
  };

  if (!spx::https::https_start(std::move(https_config), std::move(hooks))) {
    spx::log::error("app", "HTTPS bootstrap failed to start");
    spx::proxy_stop();
    return 1;
  }

  spx::log::info("app", "proxy UDP :17099 and HTTPS TCP :443 are running");
  spx::log::info("app", "press Ctrl+C to stop and restore system settings");
  while (g_running.load(std::memory_order_acquire)) {
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
  }

  spx::https::https_stop();
  spx::proxy_stop();
  spx::log::info("app", "services stopped; installed hosts and CA restored");
  return 0;
}
