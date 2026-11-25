#include <cassert>
#include <string>

#include "specialproxy/net/Net.hpp"

using spx::net::Host;

auto test_socks5_parse() -> void {
  spx::net::Socks5Config config;
  assert(spx::net::socks5_parse("127.0.0.1:1080", config));
  assert(config.enabled);
  assert(config.host == "127.0.0.1");
  assert(config.port == 1080);
  assert(config.username.empty());

  assert(spx::net::socks5_parse("proxy.local:1080:\"user\":\"pass\"", config));
  assert(config.host == "proxy.local");
  assert(config.username == "user");
  assert(config.password == "pass");

  assert(!spx::net::socks5_parse("proxy.local", config));
  assert(!spx::net::socks5_parse("proxy.local:0", config));
  assert(!spx::net::socks5_parse("proxy.local:70000", config));
  assert(!spx::net::socks5_parse(":1080", config));

  spx::net::Socks5Config invalid;
  assert(!spx::net::socks5_validate(invalid));
  invalid.host = "proxy.local";
  invalid.port = 1080;
  assert(spx::net::socks5_validate(invalid));
  invalid.password = std::string(300, 'x');
  assert(!spx::net::socks5_validate(invalid));
}

int main() {
  test_socks5_parse();
  assert(spx::net::initialize());
  {
    // Port 0 picks an ephemeral port, never conflicts with a real GT server.
    auto host = Host::create(0, 4);
    assert(host.has_value());
    const auto event = host->service(0);
    assert(!event.has_value());
  }
  spx::net::deinitialize();
  return 0;
}
