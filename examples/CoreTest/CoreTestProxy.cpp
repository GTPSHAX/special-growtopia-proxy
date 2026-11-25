/** @file
 *  @brief Consumer demo running the core entry point on Windows.
 *
 *  Starts the proxy through the exported functions only, then drives it
 *  with console toggles. Pair with CoreTestServer (fake Growtopia server)
 *  and CoreTestClient (fake Growtopia client).
 */

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>

#include "specialproxy/item/ItemData.hpp"
#include "specialproxy/proxy/Core.hpp"
#include "specialproxy/utils/Logger.hpp"

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

std::atomic_bool g_running{true};

#ifdef _WIN32
BOOL WINAPI handle_ctrl(DWORD type) {
  (void)type;
  g_running = false;
  return TRUE;
}
#endif

} // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
  SetConsoleOutputCP(CP_UTF8);
  SetConsoleCtrlHandler(handle_ctrl, TRUE);
  setvbuf(stdout, nullptr, _IONBF, 0);
#endif

  spx::proxy::ProxyConfig config;
  config.listen_port = 17099;
  config.server_port = 17091;
  config.redirect_address = "127.0.0.1";
  config.control_port = 17098;
  std::string socks5_spec;
  for (int i = 1; i < argc; ++i) {
    const std::string_view arg{argv[i]};
    if (arg == "--socks5") {
      if (i + 1 >= argc) {
        spx::log::error("demo", "--socks5 needs a host:port[:user:pass] value");
        return 1;
      }
      socks5_spec = argv[++i];
    } else if (arg.starts_with("--socks5=")) {
      socks5_spec = arg.substr(9);
    } else if (!arg.starts_with("--")) {
      config.server_address = std::string{arg};
    }
  }
  if (!socks5_spec.empty() &&
      !spx::net::socks5_parse(socks5_spec, config.socks5)) {
    spx::log::error("demo", "invalid socks5 specification");
    return 1;
  }

  const auto device = spx::device_info();
  spx::log::infof("demo",
                  "device hostname=%s os=%s platform=%s arch=%s cpus=%u",
                  device.hostname.c_str(), device.os.c_str(),
                  device.platform.c_str(), device.arch.c_str(),
                  device.cpu_count);
  spx::log::infof("demo", "core %s starting proxy entry point",
                  spx::version().data());
  if (!spx::proxy_start(config)) {
    spx::log::error("demo", "proxy_start failed");
    return 1;
  }
  const auto socks5 = spx::proxy_socks5();
  spx::log::infof("demo", "socks5 enabled=%d host=%s port=%u auth=%d",
                  socks5.enabled ? 1 : 0, socks5.host.c_str(), socks5.port,
                  socks5.has_auth ? 1 : 0);
  spx::proxy_toggle("verbose", true);
  std::printf("running on 17099 -> %s:%u control=127.0.0.1:17098 "
              "(upstream via control channel or OnSendToServer), "
              "commands: clients <text> | toggle verbose | quit\n",
              config.server_address.c_str(), config.server_port);

  std::thread console{[] {
    std::string line;
    while (g_running && std::getline(std::cin, line)) {
      if (line == "quit") {
        g_running = false;
        break;
      }
      if (line == "toggle verbose") {
        static bool verbose = true;
        verbose = !verbose;
        spx::proxy_toggle("verbose", verbose);
        std::printf("verbose %s\n", verbose ? "on" : "off");
      }
      if (line.starts_with("clients ")) {
        spx::proxy_message_clients(line.substr(8));
      }
    }
  }};

  uint32_t saved_item_crc{0};
  while (g_running) {
    const auto items = spx::item_data();
    if (items.valid && items.checksum != saved_item_crc) {
      std::error_code error;
      std::filesystem::create_directories("resources", error);
      if (!error) {
        std::ofstream output{"resources/items.dat",
                             std::ios::binary | std::ios::trunc};
        if (output) {
          output.write(reinterpret_cast<const char*>(items.bytes.data()),
                       static_cast<std::streamsize>(items.bytes.size()));
          output.close();
          if (!output.fail()) {
            saved_item_crc = items.checksum;
            spx::log::infof("demo", "items.dat saved %zu bytes crc=%08x",
                            items.bytes.size(), items.checksum);
          }
        }
      }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
  }

  spx::proxy_stop();
  if (console.joinable()) {
    console.detach();
  }
  std::printf("bye\n");
  return 0;
}
