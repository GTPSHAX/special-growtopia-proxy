#include <cassert>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "specialproxy/packet/Packet.hpp"

using namespace spx::packet;

namespace {

auto test_registry_and_decode() -> void {
  auto &registry = PacketRegistry::instance();
  registry.register_packet<game::OnConsoleMessage>();
  registry.register_packet<game::Disconnect>();
  registry.register_packet<game::OnSendToServer>();

  game::OnConsoleMessage source;
  source.message = "welcome";
  const auto frame = PacketDecoder::encode(source);
  const auto decoded = PacketDecoder::decode(frame);
  assert(decoded.has_value());
  assert((*decoded)->id() == PacketID::OnConsoleMessage);
  const auto *packet = dynamic_cast<game::OnConsoleMessage *>(decoded->get());
  assert(packet != nullptr && packet->message == "welcome");
}

auto test_text_actions() -> void {
  const auto quit = PacketDecoder::decode(
      serialize_payload(Payload{TextPayload{3u, "action|quit"}}));
  assert(quit.has_value());
  // Quit has no registered type, the generic text carrier takes it.
  assert((*quit)->id() == PacketID::Unknown);
  const auto *quit_packet = dynamic_cast<GenericTextPacket *>(quit->get());
  assert(quit_packet != nullptr && quit_packet->text == "action|quit");
  assert(quit_packet->message_type == 3u);

  const auto log = PacketDecoder::decode(
      serialize_payload(Payload{TextPayload{3u, "action|log\nmsg|hi"}}));
  assert(log.has_value());
  assert((*log)->id() == PacketID::Unknown);
}

auto test_game_packet_roundtrip() -> void {
  auto &registry = PacketRegistry::instance();
  registry.register_packet<game::Disconnect>();

  game::Disconnect source;
  const auto frame = PacketDecoder::encode(source);

  const auto parsed_payload = PacketDecoder::parse_payload(frame);
  assert(parsed_payload.has_value());
  const auto *parsed = get_payload_if<GamePayload>(*parsed_payload);
  assert(parsed != nullptr);
  assert(parsed->packet.type == static_cast<uint8_t>(PacketType::Disconnect));
  assert(parsed->extra.empty());

  const auto decoded = PacketDecoder::decode(frame);
  assert(decoded.has_value());
  assert((*decoded)->id() == PacketID::Disconnect);
}

auto test_on_send_to_server_fields() -> void {
  game::OnSendToServer source;
  source.port = 17100;
  source.token = 1234;
  source.user = 7;
  source.address = "127.0.0.1";
  source.door_id = "door";
  source.uuid_token = "uuid";
  source.login_mode = 1;
  source.username = "player";

  const auto decoded = PacketDecoder::decode(PacketDecoder::encode(source));
  assert(decoded.has_value());
  assert((*decoded)->id() == PacketID::OnSendToServer);
  const auto *packet = dynamic_cast<game::OnSendToServer *>(decoded->get());
  assert(packet != nullptr);
  assert(packet->port == 17100 && packet->token == 1234 && packet->user == 7);
  assert(packet->address == "127.0.0.1" && packet->door_id == "door");
  assert(packet->uuid_token == "uuid" && packet->login_mode == 1);
  assert(packet->username == "player");
}

auto test_on_send_to_server_signed_port() -> void {
  // The real server writes the port as a signed variant (wire type 9), like
  // luckyproxy-android. Parsing must accept it and rewriting must keep it.
  VariantList list;
  assert(list.set(0, VariantValue{std::string{"OnSendToServer"}}));
  assert(list.set(1, VariantValue{int32_t{17045}}));
  assert(list.set(2, VariantValue{int32_t{1234}}));
  assert(list.set(3, VariantValue{int32_t{7}}));
  assert(list.set(4, VariantValue{std::string{"213.179.209.175|door|uuid"}}));
  assert(list.set(5, VariantValue{int32_t{1}}));
  assert(list.set(6, VariantValue{std::string{"player"}}));

  GameUpdatePacket header{};
  header.net_id = 0xFFFFFFFFu;
  const Payload payload{VariantPayload{header, list}};
  const auto parsed = PacketDecoder::parse_payload(serialize_payload(payload));
  assert(parsed.has_value());

  game::OnSendToServer redirect;
  assert(redirect.read(*parsed));
  assert(redirect.port == 17045);
  assert(redirect.address == "213.179.209.175");
  assert(redirect.door_id == "door" && redirect.uuid_token == "uuid");

  redirect.address = "127.0.0.1";
  redirect.port = 17099;
  const auto rewritten = redirect.write();
  const auto *variant = get_payload_if<VariantPayload>(rewritten);
  assert(variant != nullptr);
  const auto *port = variant->list.get<int32_t>(1);
  assert(port != nullptr && *port == 17099);
}

auto test_on_send_to_server_short_variant() -> void {
  // A sub-server redirect may carry only six parameters. Parsing and
  // rewriting must succeed without inventing the missing username slot.
  VariantList list;
  assert(list.set(0, VariantValue{std::string{"OnSendToServer"}}));
  assert(list.set(1, VariantValue{int32_t{17002}}));
  assert(list.set(2, VariantValue{int32_t{2557785}}));
  assert(list.set(3, VariantValue{int32_t{43645948}}));
  assert(list.set(4, VariantValue{std::string{"213.179.209.175|0|"}}));
  assert(list.set(5, VariantValue{int32_t{1}}));

  const auto frame =
      serialize_payload(Payload{VariantPayload{GameUpdatePacket{}, list}});
  const auto parsed = PacketDecoder::parse_payload(frame);
  assert(parsed.has_value());

  game::OnSendToServer redirect;
  assert(redirect.read(*parsed));
  assert(redirect.port == 17002);
  assert(redirect.address == "213.179.209.175");
  assert(redirect.username.empty());

  redirect.address = "127.0.0.1";
  redirect.port = 17099;
  const auto rewritten =
      PacketDecoder::parse_payload(serialize_payload(redirect.write()));
  assert(rewritten.has_value());
  const auto *variant = get_payload_if<VariantPayload>(*rewritten);
  assert(variant != nullptr);
  assert(variant->list.size() == 6);
  const auto *new_port = variant->list.get<int32_t>(1);
  const auto *new_address = variant->list.get<std::string>(4);
  assert(new_port != nullptr && *new_port == 17099);
  assert(new_address != nullptr && *new_address == "127.0.0.1|0|");
}

auto test_tank_extra_bounds() -> void {
  // A header declaring extra bytes that are missing on the wire is malformed.
  GamePayload lying{};
  lying.packet.type = static_cast<uint8_t>(PacketType::SendMapData);
  lying.packet.flags = PacketFlagExtended;
  lying.packet.data_size = 100;
  const auto frame = serialize_payload(Payload{lying});
  assert(frame.size() == 4 + sizeof(GameUpdatePacket));
  const auto parsed = PacketDecoder::parse_payload(frame);
  assert(!parsed.has_value());
}

auto test_mismatched_payload_rejected() -> void {
  game::Disconnect game_source;
  game::OnConsoleMessage variant_source;
  const auto variant_frame = PacketDecoder::encode(variant_source);
  const auto game_frame = PacketDecoder::encode(game_source);

  // A packet must refuse a payload of another kind.
  const auto variant_decoded = PacketDecoder::parse_payload(variant_frame);
  const auto *variant_payload =
      get_payload_if<VariantPayload>(*variant_decoded);
  assert(variant_payload != nullptr);
  game::Disconnect consumer;
  assert(!consumer.read(*variant_payload));

  // The decoder derives identity from the payload, never from the type asked.
  const auto decoded = PacketDecoder::decode(game_frame);
  assert(decoded.has_value());
  assert((*decoded)->id() == PacketID::Disconnect);
}

auto test_raw_passthrough() -> void {
  std::vector<std::byte> unknown_frame{std::byte{9}, std::byte{0},
                                       std::byte{0}, std::byte{0},
                                       std::byte{1}, std::byte{2}};
  const auto parsed = PacketDecoder::parse_payload(unknown_frame);
  assert(parsed.has_value());
  const auto *raw = get_payload_if<RawPayload>(*parsed);
  assert(raw != nullptr && raw->data.size() == 6);

  const auto roundtrip = serialize_payload(*parsed);
  assert(roundtrip == unknown_frame);
}

auto test_trailing_null_tolerance() -> void {
  TextPayload payload{3u, "action|quit"};
  const auto frame = serialize_payload(Payload{payload});
  assert(frame.back() == std::byte{0});
  const auto parsed = PacketDecoder::parse_payload(frame);
  assert(parsed.has_value());
  const auto *parsed_text = get_payload_if<TextPayload>(*parsed);
  assert(parsed_text != nullptr && parsed_text->data == "action|quit");
}

auto test_corrupt_terminator() -> void {
  const std::string body = "action|input\n|text|/agZ";
  std::vector<std::byte> frame{std::byte{2}, std::byte{0}, std::byte{0},
                               std::byte{0}};
  for (const char c : body) {
    frame.push_back(static_cast<std::byte>(static_cast<unsigned char>(c)));
  }
  const auto parsed = PacketDecoder::parse_payload(frame);
  assert(parsed.has_value());
  const auto *text = get_payload_if<TextPayload>(*parsed);
  assert(text != nullptr);
  assert(text->data == "action|input\n|text|/ag");
}

} // namespace

int main() {
  test_registry_and_decode();
  test_text_actions();
  test_corrupt_terminator();
  test_game_packet_roundtrip();
  test_on_send_to_server_fields();
  test_on_send_to_server_signed_port();
  test_on_send_to_server_short_variant();
  test_tank_extra_bounds();
  test_mismatched_payload_rejected();
  test_raw_passthrough();
  test_trailing_null_tolerance();
  return 0;
}
