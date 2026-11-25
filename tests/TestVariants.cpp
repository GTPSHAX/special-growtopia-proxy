#include <cassert>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "specialproxy/packet/Packet.hpp"

using namespace spx::packet;

namespace {

auto test_value_roundtrip() -> void {
  VariantList list;
  assert(list.set(0, VariantValue{std::string{"OnConsoleMessage"}}));
  assert(list.set(1, VariantValue{42.5F}));
  assert(list.set(2, VariantValue{Vec2{1.0F, 2.0F}}));
  assert(list.set(3, VariantValue{Vec3{1.0F, 2.0F, 3.0F}}));
  assert(list.set(4, VariantValue{uint32_t{4000000000u}}));
  assert(list.set(5, VariantValue{int32_t{-7}}));
  assert(list.size() == 6);

  const auto bytes = list.serialize();
  const auto back = VariantList::parse(bytes);
  assert(back.has_value());
  assert(back->size() == 6);
  assert(*back->get<std::string>(0) == "OnConsoleMessage");
  assert(*back->get<float>(1) == 42.5F);
  const auto v2 = back->get<Vec2>(2);
  assert(v2 != nullptr && v2->x == 1.0F && v2->y == 2.0F);
  const auto v3 = back->get<Vec3>(3);
  assert(v3 != nullptr && v3->z == 3.0F);
  assert(*back->get<uint32_t>(4) == 4000000000u);
  assert(*back->get<int32_t>(5) == -7);

  // Typed access rejects a slot of another type.
  assert(back->get<int32_t>(0) == nullptr);
}

auto test_out_of_range_slots() -> void {
  VariantList list;
  assert(!list.set(kMaxVariants, VariantValue{int32_t{1}}));
  assert(list.get(kMaxVariants) == nullptr);
}

auto test_malformed_rejected() -> void {
  // Count byte above the maximum.
  std::vector<std::byte> bad_count{std::byte{8}};
  assert(!VariantList::parse(bad_count).has_value());

  // Declared element without its type byte.
  std::vector<std::byte> truncated{std::byte{1}, std::byte{0}};
  assert(!VariantList::parse(truncated).has_value());

  // String length running past the buffer.
  const uint32_t huge = 1000;
  std::vector<std::byte> bad_string{std::byte{1}, std::byte{0}, std::byte{2},
                                    std::byte{0}};
  const auto *len = reinterpret_cast<const std::byte *>(&huge);
  bad_string.insert(bad_string.end(), len, len + 4);
  bad_string.push_back(std::byte{'x'});
  assert(!VariantList::parse(bad_string).has_value());

  // Unknown wire type code.
  std::vector<std::byte> bad_type{std::byte{1}, std::byte{0}, std::byte{6}};
  bad_type.insert(bad_type.end(), 4, std::byte{0});
  assert(!VariantList::parse(bad_type).has_value());
}

auto test_indexed_layout_survives() -> void {
  // Elements carry explicit indices, gaps and order changes survive a
  // roundtrip.
  VariantList list;
  assert(list.set(5, VariantValue{int32_t{9}}));
  const auto bytes = list.serialize();
  const auto back = VariantList::parse(bytes);
  assert(back.has_value());
  assert(back->size() == 1);
  assert(*back->get<int32_t>(5) == 9);
}

auto test_typed_packet_frame() -> void {
  PacketRegistry::instance().register_packet<game::OnConsoleMessage>();
  game::OnConsoleMessage packet;
  packet.message = "hello world";
  const auto frame = PacketDecoder::encode(packet);

  const auto parsed = PacketDecoder::parse_payload(frame);
  assert(parsed.has_value());
  const auto *variant = get_payload_if<VariantPayload>(*parsed);
  assert(variant != nullptr);
  const auto *name = variant->list.get<std::string>(0);
  assert(name != nullptr && *name == "OnConsoleMessage");
  assert((variant->game_packet.flags & PacketFlagExtended) != 0u);
  assert(variant->game_packet.type ==
         static_cast<uint8_t>(PacketType::CallFunction));
  assert(variant->game_packet.net_id == 0xFFFFFFFFu);

  const auto packet_back = PacketDecoder::decode(frame);
  assert(packet_back.has_value());
  assert((*packet_back)->id() == PacketID::OnConsoleMessage);
}

} // namespace

int main() {
  test_value_roundtrip();
  test_out_of_range_slots();
  test_malformed_rejected();
  test_indexed_layout_survives();
  test_typed_packet_frame();
  return 0;
}
