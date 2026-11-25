/** @file
 *  @brief Packet identity used as the registry key and its derivation rules.
 */

#ifndef SPXCORE_PACKET_PACKET_ID_HPP
#define SPXCORE_PACKET_PACKET_ID_HPP

#include <cstdint>
#include <optional>
#include <string_view>

#include "specialproxy/packet/Payload.hpp"

namespace spx::packet {

struct TextPayload;
struct GamePayload;
struct VariantPayload;

/** Dense identity of a decodable packet, used to index registry and tables. */
enum class PacketID : uint8_t {
  Unknown = 0,
  ServerHello,
  Quit,
  QuitToExit,
  JoinRequest,
  ValidateWorld,
  Input,
  Log,
  RefreshItemData,
  Disconnect,
  TileChangeRequest,
  SendMapData,
  SendTileUpdateData,
  SendTileUpdateDataMultiple,
  SendItemDatabaseData,
  SendInventoryState,
  ModifyItemInventory,
  ItemChangeObject,
  OnConsoleMessage,
  OnTextOverlay,
  OnDialogRequest,
  OnTalkBubble,
  OnNameChanged,
  OnRequestAuctionPage,
  OnSendToServer,
  OnSpawn,
  OnSuperMainStartAcceptLogonHrdxs47254722215a,
  Count,
};

/** @brief Wire name of a packet identity.
 *
 * @param id Packet identity.
 * @return Name known to the client dispatcher, or "Unknown".
 */
[[nodiscard]] auto packet_name(PacketID id) -> std::string_view;

/** @brief Identity for a wire function name, inverse of packet_name.
 *
 * @param name Function name carried in variant index 0.
 * @return Identity, or nullopt when the name is not registered.
 */
[[nodiscard]] auto packet_id_from_name(std::string_view name) -> std::optional<PacketID>;

/** @brief Derive the identity of a text payload.
 *
 * Message type 1 maps to ServerHello, otherwise the first action field of
 * the first line is matched against the known text actions.
 *
 * @param payload Text payload.
 * @return Identity, Unknown when nothing matches.
 */
[[nodiscard]] auto derive_packet_id(const TextPayload& payload) -> PacketID;

/** @brief Derive the identity of a tank payload from its sub-type.
 *
 * @param payload Game payload.
 * @return Identity, Unknown for sub-types without a dedicated id.
 */
[[nodiscard]] auto derive_packet_id(const GamePayload& payload) -> PacketID;

/** @brief Derive the identity of a variant payload from its function name.
 *
 * @param payload Variant payload.
 * @return Identity, Unknown when index 0 does not hold a registered name.
 */
[[nodiscard]] auto derive_packet_id(const VariantPayload& payload) -> PacketID;

/** @brief Derive the identity of any payload variant.
 *
 * @param payload Payload variant.
 * @return Identity of the active alternative, Unknown when undecidable.
 */
[[nodiscard]] auto derive_packet_id(const Payload& payload) -> PacketID;

} // namespace spx::packet

#endif // SPXCORE_PACKET_PACKET_ID_HPP
