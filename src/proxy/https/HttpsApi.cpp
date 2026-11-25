/** @file
 *  @brief Process wide facade for the HTTPS bootstrap service.
 */

#include "specialproxy/https/HttpsService.hpp"

#include <memory>
#include <mutex>

namespace spx::https {
namespace {

std::mutex g_service_mutex;
std::unique_ptr<HttpsService> g_service;
net::Socks5Config g_socks5;

} // namespace

auto https_start(HttpsConfig config, HttpsHooks hooks) -> bool {
  std::lock_guard<std::mutex> lock(g_service_mutex);
  if (g_service != nullptr) {
    return false;
  }
  g_socks5 = config.socks5;
  auto service =
      std::make_unique<HttpsService>(std::move(config), std::move(hooks));
  if (!service->start()) {
    return false;
  }
  g_service = std::move(service);
  return true;
}

void https_stop() {
  std::lock_guard<std::mutex> lock(g_service_mutex);
  if (g_service == nullptr) {
    return;
  }
  g_service->stop();
  g_service.reset();
}

auto https_running() -> bool {
  std::lock_guard<std::mutex> lock(g_service_mutex);
  return g_service != nullptr && g_service->running();
}

auto https_set_socks5(const net::Socks5Config &config) -> bool {
  if (config.enabled && !net::socks5_validate(config)) {
    return false;
  }
  std::lock_guard<std::mutex> lock(g_service_mutex);
  if (g_service == nullptr) {
    return false;
  }
  g_socks5 = config;
  g_service->set_socks5(config);
  return true;
}

auto https_socks5() -> HttpsSocks5Status {
  std::lock_guard<std::mutex> lock(g_service_mutex);
  return HttpsSocks5Status{g_socks5.enabled, g_socks5.host, g_socks5.port,
                           !g_socks5.username.empty()};
}

} // namespace spx::https
