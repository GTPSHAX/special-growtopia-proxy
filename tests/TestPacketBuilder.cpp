#include <cassert>
#include <cstdint>
#include <string>
#include <vector>

#include "specialproxy/packet/Packet.hpp"

using namespace spx::packet;

namespace {

auto test_text_builder() -> void {
  builder::TextBuilder text;
  text.action("log")
      .field("msg", "hello")
      .message_type(NetMessageType::GameMessage);

  const auto payload = text.build();
  assert(payload.message_type == 3u);
  assert(payload.data == "action|log\nmsg|hello");

  const auto frame = text.frame();
  assert(frame.size() == payload.data.size() + 5u);
  assert(frame.back() == std::byte{0});

  const auto parsed = PacketDecoder::parse_payload(frame);
  assert(parsed.has_value());
  const auto *decoded = get_payload_if<TextPayload>(*parsed);
  assert(decoded != nullptr);
  assert(decoded->data == "action|log\nmsg|hello");

  const auto greeting = builder::log_frame("Connected to `2SpecialProxy");
  const auto expected = serialize_payload(
      Payload{TextPayload{3u, "action|log\nmsg|Connected to `2SpecialProxy"}});
  assert(greeting == expected);
}

auto test_variant_builder() -> void {
  builder::VariantBuilder variant;
  variant.function("OnConsoleMessage").text(1, "welcome").delay(25).net_id(7u);

  const auto payload = variant.build();
  assert(payload.game_packet.net_id == 7u);
  assert(payload.game_packet.int_data == 25);
  const auto *name = payload.list.get<std::string>(0);
  assert(name != nullptr && *name == "OnConsoleMessage");

  const auto parsed = PacketDecoder::parse_payload(variant.frame());
  assert(parsed.has_value());
  const auto *decoded = get_payload_if<VariantPayload>(*parsed);
  assert(decoded != nullptr);
  assert(decoded->game_packet.type ==
         static_cast<uint8_t>(PacketType::CallFunction));
  assert(decoded->game_packet.net_id == 7u);
  assert(decoded->game_packet.int_data == 25);
  const auto *wire_name = decoded->list.get<std::string>(0);
  assert(wire_name != nullptr && *wire_name == "OnConsoleMessage");
  const auto *wire_text = decoded->list.get<std::string>(1);
  assert(wire_text != nullptr && *wire_text == "welcome");

  builder::VariantBuilder defaults;
  defaults.function("OnTextOverlay");
  assert(defaults.build().game_packet.net_id == 0xFFFFFFFFu);
}

auto test_variant_helpers() -> void {
  const auto console = PacketDecoder::parse_payload(
      builder::console_message_frame("console text"));
  assert(console.has_value());
  const auto *console_variant = get_payload_if<VariantPayload>(*console);
  assert(console_variant != nullptr);
  const auto *console_text = console_variant->list.get<std::string>(1);
  assert(console_text != nullptr && *console_text == "console text");

  const auto overlay =
      PacketDecoder::parse_payload(builder::text_overlay_frame("overlay text"));
  assert(overlay.has_value());
  const auto *overlay_variant = get_payload_if<VariantPayload>(*overlay);
  assert(overlay_variant != nullptr);
  const auto *overlay_name = overlay_variant->list.get<std::string>(0);
  assert(overlay_name != nullptr && *overlay_name == "OnTextOverlay");

  const auto bubble = PacketDecoder::parse_payload(
      builder::talk_bubble_frame(42u, "bubble text"));
  assert(bubble.has_value());
  const auto *bubble_variant = get_payload_if<VariantPayload>(*bubble);
  assert(bubble_variant != nullptr);
  const auto *bubble_id = bubble_variant->list.get<uint32_t>(1);
  const auto *bubble_text = bubble_variant->list.get<std::string>(2);
  assert(bubble_id != nullptr && *bubble_id == 42u);
  assert(bubble_text != nullptr && *bubble_text == "bubble text");

  const auto dialog = PacketDecoder::parse_payload(
      builder::dialog_request_frame("add_button|buy|Buy|0|"));
  assert(dialog.has_value());
  const auto *dialog_variant = get_payload_if<VariantPayload>(*dialog);
  assert(dialog_variant != nullptr);
  const auto *dialog_body = dialog_variant->list.get<std::string>(1);
  assert(dialog_body != nullptr && *dialog_body == "add_button|buy|Buy|0|");
}

auto test_send_to_server_builder() -> void {
  builder::SendToServerFields fields;
  fields.port = 17099;
  fields.token = -3;
  fields.user = 12;
  fields.address = "127.0.0.1";
  fields.door_id = "EXIT";
  fields.uuid_token = "uuid-1";
  fields.login_mode = 1;
  fields.username = "player";

  const auto frame = builder::send_to_server_frame(fields);
  const auto parsed = PacketDecoder::parse_payload(frame);
  assert(parsed.has_value());
  const auto *decoded = get_payload_if<VariantPayload>(*parsed);
  assert(decoded != nullptr);
  const auto *name = decoded->list.get<std::string>(0);
  assert(name != nullptr && *name == "OnSendToServer");
  const auto *port = decoded->list.get<int32_t>(1);
  assert(port != nullptr && *port == 17099);
  const auto *combined = decoded->list.get<std::string>(4);
  assert(combined != nullptr && *combined == "127.0.0.1|EXIT|uuid-1");

  game::OnSendToServer typed;
  typed.port = static_cast<uint16_t>(fields.port);
  typed.token = fields.token;
  typed.user = fields.user;
  typed.address = fields.address;
  typed.door_id = fields.door_id;
  typed.uuid_token = fields.uuid_token;
  typed.login_mode = fields.login_mode;
  typed.username = fields.username;
  assert(frame == serialize_payload(typed.write()));
}

auto test_game_builder() -> void {
  const auto extra =
      std::vector<std::byte>{std::byte{0x01}, std::byte{0x02}, std::byte{0x03}};
  builder::GameBuilder game;
  game.type(PacketType::SetIconState)
      .net_id(9u)
      .int_x(2)
      .position(1.0F, 2.0F)
      .extra(extra);

  const auto parsed = PacketDecoder::parse_payload(game.frame());
  assert(parsed.has_value());
  const auto *decoded = get_payload_if<GamePayload>(*parsed);
  assert(decoded != nullptr);
  assert(decoded->packet.type ==
         static_cast<uint8_t>(PacketType::SetIconState));
  assert(decoded->packet.net_id == 9u);
  assert(decoded->packet.int_x == 2);
  assert(decoded->packet.data_size == extra.size());
  assert((decoded->packet.flags & PacketFlagExtended) != 0u);
  assert(decoded->extra == extra);

  const auto icon =
      PacketDecoder::parse_payload(builder::icon_state_frame(9u, 0));
  assert(icon.has_value());
  const auto *icon_game = get_payload_if<GamePayload>(*icon);
  assert(icon_game != nullptr);
  assert(icon_game->packet.int_x == 0);
  assert(icon_game->extra.empty());
}

} // namespace

auto main() -> int {
  test_text_builder();
  test_variant_builder();
  test_variant_helpers();
  test_send_to_server_builder();
  test_game_builder();
  return 0;
}
