/** @file
 *  @brief Factory registry mapping PacketID to packet constructors.
 */

#ifndef SPXCORE_PACKET_PACKET_REGISTRY_HPP
#define SPXCORE_PACKET_PACKET_REGISTRY_HPP

#include <array>
#include <cstddef>
#include <memory>
#include <type_traits>

#include "specialproxy/packet/PacketHelper.hpp"
#include "specialproxy/packet/PacketID.hpp"
#include "specialproxy/packet/PacketType.hpp"
#include "specialproxy/packet/Payload.hpp"

namespace spx::packet {

/** Factory returning a fresh empty packet for one identity. */
using PacketFactory = std::shared_ptr<IPacket> (*)();

/** Identity keyed factory table with O(1) lookup and no heap allocation. */
class PacketRegistry {
 public:
  /** @brief Process wide registry instance.
   *
   * Registration is expected once at startup from a single thread.
   *
   * @return Shared registry.
   */
  static auto instance() -> PacketRegistry&;

  /** @brief Register a concrete packet type under its baked identity.
   *
   * @tparam T Concrete packet type deriving from IPacket.
   */
  template <typename T>
  void register_packet() {
    static_assert(std::is_base_of_v<IPacket, T>,
                  "Registered type must derive from IPacket");
    factories_[index(T::ID)] = [] {
      return std::static_pointer_cast<IPacket>(std::make_shared<T>());
    };
  }

  /** @brief Create a packet by identity.
   *
   * @param id Registered identity.
   * @return Fresh packet, or null when the identity is unregistered.
   */
  [[nodiscard]] auto create(PacketID id) const -> std::shared_ptr<IPacket> {
    const auto slot = index(id);
    if (slot >= factories_.size() || factories_[slot] == nullptr) {
      return nullptr;
    }
    return factories_[slot]();
  }

  /** @brief Derive the identity of a payload and create its packet.
   *
   * @param payload Decoded payload.
   * @return Fresh packet, or null when the identity is unregistered.
   */
  [[nodiscard]] auto create(const Payload& payload) const
      -> std::shared_ptr<IPacket> {
    return create(derive_packet_id(payload));
  }

 private:
  static auto index(PacketID id) -> size_t {
    return static_cast<size_t>(id);
  }

  std::array<PacketFactory, static_cast<size_t>(PacketID::Count)> factories_{};
};

} // namespace spx::packet

#endif // SPXCORE_PACKET_PACKET_REGISTRY_HPP
