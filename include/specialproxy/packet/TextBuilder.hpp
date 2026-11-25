/** @file
 *  @brief Fluent construction of text packet frames.
 */

#ifndef SPXCORE_PACKET_BUILDER_TEXT_BUILDER_HPP
#define SPXCORE_PACKET_BUILDER_TEXT_BUILDER_HPP

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "specialproxy/packet/Payload.hpp"
#include "specialproxy/packet/PacketType.hpp"

namespace spx::packet::builder {

/** Builds a text frame as an action line followed by optional field lines. */
class TextBuilder {
 public:
  /**
   * @brief Set the message type written in the frame prefix.
   *
   * @param[in] type Message type carried in the prefix.
   * @return This builder.
   */
  auto message_type(NetMessageType type) -> TextBuilder& {
    type_ = type;
    return *this;
  }

  /**
   * @brief Replace the body with a single action directive.
   *
   * @param[in] name Action name written after the action marker.
   * @return This builder.
   */
  auto action(std::string_view name) -> TextBuilder& {
    body_.assign("action|");
    body_.append(name);
    return *this;
  }

  /**
   * @brief Append one pipe separated field line to the body.
   *
   * @param[in] key Field name.
   * @param[in] value Field value.
   * @return This builder.
   */
  auto field(std::string_view key, std::string_view value) -> TextBuilder& {
    if (!body_.empty()) {
      body_.push_back('\n');
    }
    body_.append(key);
    body_.push_back('|');
    body_.append(value);
    return *this;
  }

  /**
   * @brief Replace the body with verbatim text.
   *
   * @param[in] text Complete text body.
   * @return This builder.
   */
  auto body(std::string_view text) -> TextBuilder& {
    body_.assign(text);
    return *this;
  }

  /**
   * @brief Materialize the configured text payload.
   *
   * @return Payload with the stored message type and body.
   */
  [[nodiscard]] auto build() const -> TextPayload {
    return TextPayload{static_cast<uint32_t>(type_), body_};
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
  NetMessageType type_{NetMessageType::GameMessage};
  std::string body_;
};

/**
 * @brief Build a game log line for a client chat window.
 *
 * @param[in] message Text shown to the player.
 * @return Complete wire frame.
 */
[[nodiscard]] inline auto log_frame(std::string_view message)
    -> std::vector<std::byte> {
  TextBuilder builder;
  builder.action("log").field("msg", message);
  return builder.frame();
}

} // namespace spx::packet::builder

#endif // SPXCORE_PACKET_BUILDER_TEXT_BUILDER_HPP
