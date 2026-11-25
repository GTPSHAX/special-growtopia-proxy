/** @file
 *  @brief One proxied client paired with its server connection.
 */

#ifndef SPXCORE_PROXY_SESSION_HPP
#define SPXCORE_PROXY_SESSION_HPP

#include <cstdint>
#include <string>

#include "specialproxy/net/Net.hpp"
#include "enet.h"

namespace spx::proxy {

/** Pair of peers forming one proxied Growtopia session. */
struct Session {
  /** Growtopia client peer, set when the session is created. */
  ENetPeer* client{nullptr};
  /** Growtopia server peer, set when the connect request is queued. */
  ENetPeer* server{nullptr};
  /** Monotonic id for logging and lookup. */
  uint32_t id{0};
  /** Server target in use, defaults to config until a redirect arrives. */
  std::string server_address;
  /** Server target port in use. */
  uint16_t server_port{0};
  /** Dedicated upstream host for this session.
   *
   * The Growtopia new-header engine derives packet integrity from the first
   * peer bound to a host, so each session needs its own host when targets
   * differ after a redirect.
   */
  std::optional<net::Host> upstream_host;
};

} // namespace spx::proxy

#endif // SPXCORE_PROXY_SESSION_HPP
