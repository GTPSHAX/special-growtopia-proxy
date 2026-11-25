/** @file
 *  @brief Fluent construction of variant function call frames.
 */

#ifndef SPXCORE_PACKET_BUILDER_VARIANT_BUILDER_HPP
#define SPXCORE_PACKET_BUILDER_VARIANT_BUILDER_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "specialproxy/packet/PacketType.hpp"
#include "specialproxy/packet/PacketVariant.hpp"
#include "specialproxy/packet/Payload.hpp"

namespace spx::packet::builder {

/** Builds a CallFunction frame holding a named variant parameter list. */
class VariantBuilder {
 public:
  /** @brief Prepare the sender marker expected by the game client. */
  VariantBuilder() { header_.net_id = 0xFFFFFFFFu; }

  /**
   * @brief Set the function name stored in variant index zero.
   *
   * @param[in] name Wire name of the function to invoke.
   * @return This builder.
   */
  auto function(std::string_view name) -> VariantBuilder& {
    return text(0, name);
  }

  /**
   * @brief Store one variant value at a wire index.
   *
   * @param[in] index Wire index of the parameter.
   * @param[in] value Value to store.
   * @return This builder.
   */
  auto param(size_t index, VariantValue value) -> VariantBuilder& {
    list_.set(index, std::move(value));
    return *this;
  }

  /**
   * @brief Store one text value at a wire index.
   *
   * @param[in] index Wire index of the parameter.
   * @param[in] value Text to store.
   * @return This builder.
   */
  auto text(size_t index, std::string_view value) -> VariantBuilder& {
    return param(index, VariantValue{std::string{value}});
  }

  /**
   * @brief Store one signed integer value at a wire index.
   *
   * @param[in] index Wire index of the parameter.
   * @param[in] value Value to store.
   * @return This builder.
   */
  auto param_i32(size_t index, int32_t value) -> VariantBuilder& {
    return param(index, VariantValue{value});
  }

  /**
   * @brief Store one unsigned integer value at a wire index.
   *
   * @param[in] index Wire index of the parameter.
   * @param[in] value Value to store.
   * @return This builder.
   */
  auto param_u32(size_t index, uint32_t value) -> VariantBuilder& {
    return param(index, VariantValue{value});
  }

  /**
   * @brief Store one float value at a wire index.
   *
   * @param[in] index Wire index of the parameter.
   * @param[in] value Value to store.
   * @return This builder.
   */
  auto param_float(size_t index, float value) -> VariantBuilder& {
    return param(index, VariantValue{value});
  }

  /**
   * @brief Override the sender net id in the tank header.
   *
   * @param[in] id Net id written to the header.
   * @return This builder.
   */
  auto net_id(uint32_t id) -> VariantBuilder& {
    header_.net_id = id;
    return *this;
  }

  /**
   * @brief Set the variant call delay in milliseconds.
   *
   * @param[in] milliseconds Delay written to the integer field.
   * @return This builder.
   */
  auto delay(int32_t milliseconds) -> VariantBuilder& {
    header_.int_data = milliseconds;
    return *this;
  }

  /**
   * @brief Materialize the configured variant payload.
   *
   * @return Payload with the tank header and parameter list.
   */
  [[nodiscard]] auto build() const -> VariantPayload {
    return VariantPayload{header_, list_};
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
  VariantList list_;
};

/** Parameters of a sub-server redirect function call. */
struct SendToServerFields {
  int32_t port{0};            ///< Port of the real game server.
  int32_t token{0};           ///< Session token supplied by the server.
  int32_t user{0};            ///< User identifier supplied by the server.
  std::string address;        ///< Address of the real game server.
  std::string door_id;        ///< Door or world identifier.
  std::string uuid_token;     ///< UUID token supplied by the server.
  int32_t login_mode{0};      ///< Login mode supplied by the server.
  std::string username;       ///< Player account name.
};

/**
 * @brief Build an OnConsoleMessage frame for the client chat window.
 *
 * @param[in] message Text shown in the console area.
 * @return Complete wire frame.
 */
[[nodiscard]] inline auto console_message_frame(std::string_view message)
    -> std::vector<std::byte> {
  VariantBuilder builder;
  builder.function("OnConsoleMessage").text(1, message);
  return builder.frame();
}

/**
 * @brief Build an OnTextOverlay frame for a centered screen message.
 *
 * @param[in] message Text shown as an overlay.
 * @return Complete wire frame.
 */
[[nodiscard]] inline auto text_overlay_frame(std::string_view message)
    -> std::vector<std::byte> {
  VariantBuilder builder;
  builder.function("OnTextOverlay").text(1, message);
  return builder.frame();
}

/**
 * @brief Build an OnTalkBubble frame attached to an avatar.
 *
 * @param[in] net_id Net id of the speaking avatar.
 * @param[in] message Bubble text.
 * @return Complete wire frame.
 */
[[nodiscard]] inline auto talk_bubble_frame(uint32_t net_id,
                                            std::string_view message)
    -> std::vector<std::byte> {
  VariantBuilder builder;
  builder.function("OnTalkBubble")
      .param_u32(1, net_id)
      .text(2, message);
  return builder.frame();
}

/**
 * @brief Build an OnDialogRequest frame carrying a dialog body.
 *
 * @param[in] dialog Pipe delimited dialog definition.
 * @return Complete wire frame.
 */
[[nodiscard]] inline auto dialog_request_frame(std::string_view dialog)
    -> std::vector<std::byte> {
  VariantBuilder builder;
  builder.function("OnDialogRequest").text(1, dialog);
  return builder.frame();
}

/**
 * @brief Build an OnSendToServer redirect frame.
 *
 * @param[in] fields Connection and login parameters of the target server.
 * @return Complete wire frame.
 */
[[nodiscard]] inline auto send_to_server_frame(
    const SendToServerFields& fields) -> std::vector<std::byte> {
  std::string combined = fields.address;
  combined.push_back('|');
  combined += fields.door_id;
  combined.push_back('|');
  combined += fields.uuid_token;

  VariantBuilder builder;
  builder.function("OnSendToServer")
      .param_i32(1, fields.port)
      .param_i32(2, fields.token)
      .param_i32(3, fields.user)
      .text(4, combined)
      .param_i32(5, fields.login_mode)
      .text(6, fields.username);
  return builder.frame();
}

} // namespace spx::packet::builder

#endif // SPXCORE_PACKET_BUILDER_VARIANT_BUILDER_HPP
