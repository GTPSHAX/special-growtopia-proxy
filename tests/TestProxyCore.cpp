/** @file
 *  @brief Tests for the external bootstrap API of the proxy core.
 */

#include <cassert>
#include <cstdlib>

#include "specialproxy/proxy/ControlChannel.hpp"
#include "specialproxy/proxy/Core.hpp"

int main() {
  const auto initial = spx::proxy_upstream();
  assert(initial.server.empty());
  assert(initial.port == 0);

  assert(spx::proxy_set_upstream("1.2.3.4", 17046));
  const auto first = spx::proxy_upstream();
  assert(first.server == "1.2.3.4");
  assert(first.port == 17046);

  assert(spx::proxy_set_upstream("203.0.113.9", 17097));
  const auto second = spx::proxy_upstream();
  assert(second.server == "203.0.113.9");
  assert(second.port == 17097);

  spx::proxy::ControlUpstream update;
  assert(spx::proxy::parse_control_message("upstream|1.2.3.4|17046", update));
  assert(update.server == "1.2.3.4");
  assert(update.port == 17046);
  assert(spx::proxy::parse_control_message("upstream| 203.0.113.9 |17097\n",
                                           update));
  assert(update.server == "203.0.113.9");
  assert(update.port == 17097);
  assert(!spx::proxy::parse_control_message("upstream||17046", update));
  assert(!spx::proxy::parse_control_message("upstream|1.2.3.4|0", update));
  assert(!spx::proxy::parse_control_message("upstream|1.2.3.4|70000", update));
  assert(!spx::proxy::parse_control_message("upstream|1.2.3.4|abc", update));
  assert(!spx::proxy::parse_control_message("garbage", update));

  const auto no_socks = spx::proxy_socks5();
  assert(!no_socks.enabled);
  assert(no_socks.host.empty());
  assert(!no_socks.has_auth);

  spx::net::Socks5Config socks;
  socks.enabled = true;
  socks.host = "127.0.0.1";
  socks.port = 1080;
  socks.username = "user";
  socks.password = "secret";
  assert(spx::proxy_set_socks5(socks));
  const auto active = spx::proxy_socks5();
  assert(active.enabled);
  assert(active.host == "127.0.0.1");
  assert(active.port == 1080);
  assert(active.has_auth);

  socks.host.clear();
  assert(!spx::proxy_set_socks5(socks));
  const auto unchanged = spx::proxy_socks5();
  assert(unchanged.host == "127.0.0.1");

  return EXIT_SUCCESS;
}
