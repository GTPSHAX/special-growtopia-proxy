/** @file
 *  @brief Typed packets for the server side of the game protocol.
 */

#ifndef SPXCORE_PACKET_GAME_SERVER_HPP
#define SPXCORE_PACKET_GAME_SERVER_HPP

#include <cstdint>
#include <string>
#include <utility>

#include "specialproxy/packet/GenericPackets.hpp"
#include "specialproxy/packet/PacketHelper.hpp"
#include "specialproxy/packet/PacketID.hpp"
#include "specialproxy/packet/PacketType.hpp"

namespace spx::packet::game {

/** Console line shown in the client chat window. */
struct OnConsoleMessage : VariantPacket<PacketID::OnConsoleMessage> {
  std::string message;

  auto read(const Payload& payload) -> bool override {
    const auto* parsed = get_payload_if<VariantPayload>(payload);
    if (parsed == nullptr) {
      return false;
    }
    game_packet = parsed->game_packet;
    list = parsed->list;
    const auto* text = list.get<std::string>(1);
    if (text == nullptr) {
      return false;
    }
    message = *text;
    return true;
  }

  auto write() -> Payload override {
    set_function_name();
    list.set(1, VariantValue{message});
    return VariantPayload{game_packet, list};
  }
};

/** Center screen overlay text. */
struct OnTextOverlay : VariantPacket<PacketID::OnTextOverlay> {
  std::string message;

  auto read(const Payload& payload) -> bool override {
    const auto* parsed = get_payload_if<VariantPayload>(payload);
    if (parsed == nullptr) {
      return false;
    }
    game_packet = parsed->game_packet;
    list = parsed->list;
    const auto* text = list.get<std::string>(1);
    if (text == nullptr) {
      return false;
    }
    message = *text;
    return true;
  }

  auto write() -> Payload override {
    set_function_name();
    list.set(1, VariantValue{message});
    return VariantPayload{game_packet, list};
  }
};

/** Chat bubble attached to an avatar net id. */
struct OnTalkBubble : VariantPacket<PacketID::OnTalkBubble> {
  uint32_t net_id{0};
  std::string message;

  auto read(const Payload& payload) -> bool override {
    const auto* parsed = get_payload_if<VariantPayload>(payload);
    if (parsed == nullptr) {
      return false;
    }
    game_packet = parsed->game_packet;
    list = parsed->list;
    const auto* id = list.get<uint32_t>(1);
    const auto* text = list.get<std::string>(2);
    if (id == nullptr || text == nullptr) {
      return false;
    }
    net_id = *id;
    message = *text;
    return true;
  }

  auto write() -> Payload override {
    set_function_name();
    list.set(1, VariantValue{net_id});
    list.set(2, VariantValue{message});
    return VariantPayload{game_packet, list};
  }
};

/** Display name change for an avatar. */
struct OnNameChanged : VariantPacket<PacketID::OnNameChanged> {
  std::string name;

  auto read(const Payload& payload) -> bool override {
    const auto* parsed = get_payload_if<VariantPayload>(payload);
    if (parsed == nullptr) {
      return false;
    }
    game_packet = parsed->game_packet;
    list = parsed->list;
    const auto* text = list.get<std::string>(1);
    if (text == nullptr) {
      return false;
    }
    name = *text;
    return true;
  }

  auto write() -> Payload override {
    set_function_name();
    list.set(1, VariantValue{name});
    return VariantPayload{game_packet, list};
  }
};

/** Opens a client dialog from raw dialog definition text. */
struct OnDialogRequest : VariantPacket<PacketID::OnDialogRequest> {
  std::string dialog;

  auto read(const Payload& payload) -> bool override {
    const auto* parsed = get_payload_if<VariantPayload>(payload);
    if (parsed == nullptr) {
      return false;
    }
    game_packet = parsed->game_packet;
    list = parsed->list;
    const auto* text = list.get<std::string>(1);
    if (text == nullptr) {
      return false;
    }
    dialog = *text;
    return true;
  }

  auto write() -> Payload override {
    set_function_name();
    list.set(1, VariantValue{dialog});
    return VariantPayload{game_packet, list};
  }
};

/** Sub-server redirect carrying connection and login parameters. */
struct OnSendToServer : VariantPacket<PacketID::OnSendToServer> {
  uint16_t port{0};
  int32_t token{0};
  int32_t user{0};
  std::string address;
  std::string door_id;
  std::string uuid_token;
  int32_t login_mode{0};
  std::string username;

  auto read(const Payload& payload) -> bool override {
    const auto* parsed = get_payload_if<VariantPayload>(payload);
    if (parsed == nullptr) {
      return false;
    }
    game_packet = parsed->game_packet;
    list = parsed->list;
    // Servers send either six or seven parameters. The trailing login mode
    // and username are optional on sub-server redirects.
    if (list.size() < 5) {
      return false;
    }
    const auto* port_value = list.get<int32_t>(1);
    const auto* token_value = list.get<int32_t>(2);
    const auto* user_value = list.get<int32_t>(3);
    const auto* combined = list.get<std::string>(4);
    if (port_value == nullptr || token_value == nullptr ||
        user_value == nullptr || combined == nullptr) {
      return false;
    }
    port = static_cast<uint16_t>(*port_value);
    token = *token_value;
    user = *user_value;
    login_mode = 0;
    username.clear();
    if (const auto* mode_value = list.get<int32_t>(5)) {
      login_mode = *mode_value;
    }
    if (const auto* name_value = list.get<std::string>(6)) {
      username = *name_value;
    }

    // Variant 4 packs the address, door id and uuid token with pipes.
    const size_t first = combined->find('|');
    const size_t second = first == std::string::npos
                              ? std::string::npos
                              : combined->find('|', first + 1);
    address = combined->substr(0, first);
    if (first != std::string::npos) {
      door_id = first == second
                    ? combined->substr(first + 1)
                    : combined->substr(first + 1, second - first - 1);
    }
    if (second != std::string::npos) {
      uuid_token = combined->substr(second + 1);
    }
    return true;
  }

  auto write() -> Payload override {
    set_function_name();
    std::string combined = address;
    combined.push_back('|');
    combined += door_id;
    combined.push_back('|');
    combined += uuid_token;
    list.set(1, VariantValue{int32_t{port}});
    list.set(2, VariantValue{token});
    list.set(3, VariantValue{user});
    list.set(4, VariantValue{std::move(combined)});
    list.set(5, VariantValue{login_mode});
    if (!username.empty()) {
      list.set(6, VariantValue{username});
    }
    return VariantPayload{game_packet, list};
  }
};

/** Login completion carrying the item database and CDN parameters. */
struct OnSuperMainStartAcceptLogonHrdxs47254722215a
    : VariantPacket<PacketID::OnSuperMainStartAcceptLogonHrdxs47254722215a> {
  uint32_t item_hash{0};
  std::string cdn_host;
  std::string cache_path;
  std::string blocked_packages;
  std::string settings;
  uint32_t player_tribute_hash{0};

  auto read(const Payload& payload) -> bool override {
    const auto* parsed = get_payload_if<VariantPayload>(payload);
    if (parsed == nullptr) {
      return false;
    }
    game_packet = parsed->game_packet;
    list = parsed->list;
    if (list.size() < 4) {
      return false;
    }
    const auto* hash_value = list.get<uint32_t>(1);
    const auto* host_value = list.get<std::string>(2);
    const auto* path_value = list.get<std::string>(3);
    if (hash_value == nullptr || host_value == nullptr || path_value == nullptr) {
      return false;
    }
    item_hash = *hash_value;
    cdn_host = *host_value;
    cache_path = *path_value;
    // Older protocols send fewer variants, later fields are optional.
    if (const auto* blocked = list.get<std::string>(4)) {
      blocked_packages = *blocked;
    }
    if (const auto* config = list.get<std::string>(5)) {
      settings = *config;
    }
    if (const auto* tribute = list.get<uint32_t>(6)) {
      player_tribute_hash = *tribute;
    }
    return true;
  }

  auto write() -> Payload override {
    set_function_name();
    list.set(1, VariantValue{item_hash});
    list.set(2, VariantValue{cdn_host});
    list.set(3, VariantValue{cache_path});
    if (!blocked_packages.empty()) {
      list.set(4, VariantValue{blocked_packages});
    }
    if (!settings.empty()) {
      list.set(5, VariantValue{settings});
    }
    list.set(6, VariantValue{player_tribute_hash});
    return VariantPayload{game_packet, list};
  }
};

/** Server requested disconnect, sent as a bare tank header. */
struct Disconnect : GamePacket<PacketID::Disconnect, PacketType::Disconnect> {
  auto read(const Payload& payload) -> bool override {
    const auto* parsed = get_payload_if<GamePayload>(payload);
    if (parsed == nullptr) {
      return false;
    }
    game_packet = parsed->packet;
    extra = parsed->extra;
    return true;
  }

  auto write() -> Payload override {
    game_packet.type = static_cast<uint8_t>(PACKET_TYPE);
    game_packet.net_id = 0xFFFFFFFFu;
    return GamePayload{game_packet, extra};
  }
};

} // namespace spx::packet::game

#endif // SPXCORE_PACKET_GAME_SERVER_HPP
