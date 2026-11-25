/** @file
 *  @brief Fluent construction of raw tank packet frames.
 */

#ifndef SPXCORE_PACKET_BUILDER_GAME_BUILDER_HPP
#define SPXCORE_PACKET_BUILDER_GAME_BUILDER_HPP

#include <cstdint>
#include <utility>
#include <vector>

#include "specialproxy/packet/PacketType.hpp"
#include "specialproxy/packet/Payload.hpp"

namespace spx::packet::builder {

/** Builds a tank frame by setting header fields and optional extra data. */
class GameBuilder {
 public:
  /**
   * @brief Set the tank sub-type.
   *
   * @param[in] value Sub-type written to the header.
   * @return This builder.
   */
  auto type(PacketType value) -> GameBuilder& {
    header_.type = static_cast<uint8_t>(value);
    return *this;
  }

  /**
   * @brief Set the sender net id.
   *
   * @param[in] id Net id written to the header.
   * @return This builder.
   */
  auto net_id(uint32_t id) -> GameBuilder& {
    header_.net_id = id;
    return *this;
  }

  /**
   * @brief Set the tank flag bit mask.
   *
   * @param[in] value Flag bits written to the header.
   * @return This builder.
   */
  auto flags(uint32_t value) -> GameBuilder& {
    header_.flags = value;
    return *this;
  }

  /**
   * @brief Set the integer data field.
   *
   * @param[in] value Value written to the integer field.
   * @return This builder.
   */
  auto int_data(int32_t value) -> GameBuilder& {
    header_.int_data = value;
    return *this;
  }

  /**
   * @brief Set the primary tile x field.
   *
   * @param[in] value Tile coordinate written to the header.
   * @return This builder.
   */
  auto int_x(int32_t value) -> GameBuilder& {
    header_.int_x = value;
    return *this;
  }

  /**
   * @brief Set the primary tile y field.
   *
   * @param[in] value Tile coordinate written to the header.
   * @return This builder.
   */
  auto int_y(int32_t value) -> GameBuilder& {
    header_.int_y = value;
    return *this;
  }

  /**
   * @brief Set the float parameter field.
   *
   * @param[in] value Value written to the float field.
   * @return This builder.
   */
  auto float_var(float value) -> GameBuilder& {
    header_.float_var = value;
    return *this;
  }

  /**
   * @brief Set the primary position fields.
   *
   * @param[in] x Horizontal position.
   * @param[in] y Vertical position.
   * @return This builder.
   */
  auto position(float x, float y) -> GameBuilder& {
    header_.pos_x = x;
    header_.pos_y = y;
    return *this;
  }

  /**
   * @brief Attach bytes written after the tank header.
   *
   * @param[in] data Extra data to append.
   * @return This builder.
   */
  auto extra(std::vector<std::byte> data) -> GameBuilder& {
    extra_ = std::move(data);
    return *this;
  }

  /**
   * @brief Materialize the configured tank payload.
   *
   * @return Payload with the tank header and extra data.
   */
  [[nodiscard]] auto build() const -> GamePayload {
    return GamePayload{header_, extra_};
  }

  /**
   * @brief Materialize a complete wire frame.
   *
   * @return Frame bytes, empty when a size limit is exceeded.
   */
  [[nodiscard]] auto frame() const -> std::vector<std::byte> {
    return serialize_payload(build());
  }

 private:
  GameUpdatePacket header_{};
  std::vector<std::byte> extra_;
};

/**
 * @brief Build a SetIconState frame used for avatar icon toggles.
 *
 * @param[in] net_id Net id of the avatar.
 * @param[in] state Icon state written to the integer field.
 * @return Complete wire frame.
 */
[[nodiscard]] inline auto icon_state_frame(uint32_t net_id, int32_t state)
    -> std::vector<std::byte> {
  GameBuilder builder;
  builder.type(PacketType::SetIconState).net_id(net_id).int_x(state);
  return builder.frame();
}

} // namespace spx::packet::builder

#endif // SPXCORE_PACKET_BUILDER_GAME_BUILDER_HPP
