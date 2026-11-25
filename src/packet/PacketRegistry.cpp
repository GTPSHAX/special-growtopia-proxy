/** @file
 *  @brief Single process wide packet registry instance.
 */

#include "specialproxy/packet/PacketRegistry.hpp"

namespace spx::packet {

auto PacketRegistry::instance() -> PacketRegistry & {
  static PacketRegistry registry;
  return registry;
}

} // namespace spx::packet
