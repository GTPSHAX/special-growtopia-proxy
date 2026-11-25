/** @file
 *  @brief Growtopia wire enums and the packed tank header struct.
 */

#ifndef SPXCORE_PACKET_PACKET_TYPE_HPP
#define SPXCORE_PACKET_PACKET_TYPE_HPP

#include <cstdint>

namespace spx::packet {

/** Message type stored in the first 4 bytes of every Growtopia ENet frame. */
enum class NetMessageType : uint32_t {
  Unknown = 0,
  /** Login hello from the client. */
  ServerHello = 1,
  /** Plain text command packet, for example "action|join_request". */
  GenericText = 2,
  /** Game message text packet, for example "action|log". */
  GameMessage = 3,
  /** Binary tank packet carrying a PacketType sub-header. */
  GamePacket = 4,
  /** Error text packet. */
  Error = 5,
  /** Telemetry text packet. */
  Track = 6,
  /** Client log request text packet. */
  ClientLogRequest = 7,
  /** Client log response text packet. */
  ClientLogResponse = 8,
};

/** Tank packet sub-type written at the start of the tank header. */
enum class PacketType : uint8_t {
  State = 0,
  /** Variant list function call. */
  CallFunction = 1,
  UpdateStatus = 2,
  TileChangeRequest = 3,
  SendMapData = 4,
  SendTileUpdateData = 5,
  SendTileUpdateDataMultiple = 6,
  TileActivateRequest = 7,
  TileApplyDamage = 8,
  SendInventoryState = 9,
  ItemActivateRequest = 10,
  ItemActivateObjectRequest = 11,
  SendTileTreeState = 12,
  ModifyItemInventory = 13,
  ItemChangeObject = 14,
  SendLock = 15,
  SendItemDatabaseData = 16,
  SendParticleEffect = 17,
  SetIconState = 18,
  ItemEffect = 19,
  SetCharacterState = 20,
  PingReply = 21,
  PingRequest = 22,
  GotPunched = 23,
  AppCheckResponse = 24,
  AppIntegrityFail = 25,
  Disconnect = 26,
  BattleJoin = 27,
  BattleEvent = 28,
  UseDoor = 29,
  SendParental = 30,
  GoneFishin = 31,
  Steam = 32,
  PetBattle = 33,
  Npc = 34,
  Special = 35,
  SendParticleEffectV2 = 36,
  ActiveArrowToItem = 37,
  SelectTileIndex = 38,
};

/** Flag bits stored in the 32-bit flags field at tank offset 12. */
enum PacketFlag : uint32_t {
  PacketFlagNone = 0,
  /** Set when extra data follows the 56-byte header. */
  PacketFlagExtended = 1u << 3,
  PacketFlagRotateLeft = 1u << 4,
  PacketFlagOnSolid = 1u << 5,
  PacketFlagOnFireDamage = 1u << 6,
  PacketFlagOnJump = 1u << 7,
  PacketFlagOnKilled = 1u << 8,
  PacketFlagOnPunched = 1u << 9,
  PacketFlagOnPlaced = 1u << 10,
  PacketFlagOnTileAction = 1u << 11,
  PacketFlagOnGotPunched = 1u << 12,
  PacketFlagOnRespawned = 1u << 13,
  PacketFlagOnCollectObject = 1u << 14,
  PacketFlagOnTrampoline = 1u << 15,
  PacketFlagOnDamage = 1u << 16,
  PacketFlagOnSlide = 1u << 17,
  PacketFlagOnWallHang = 1u << 21,
  PacketFlagOnAcidDamage = 1u << 26,
};

/**
 * @brief Packed 56-byte tank header written after the 4-byte message type.
 *
 * The struct maps one-to-one onto the wire, so serializing is a single
 * memcpy. Variant function calls store the delay in int_data and mark the
 * extended flag; extra data then follows the struct.
 */
#pragma pack(push, 1)
struct GameUpdatePacket {
  /** Tank sub-type, see PacketType. */
  uint8_t type{0};
  /** Wire padding, always zero. */
  uint8_t pad0_[3]{0, 0, 0};
  /** Sender net id, or the object change type for map packets. */
  uint32_t net_id{0};
  /** Item net id for inventory scoped updates. */
  int32_t item_net_id{0};
  /** Flag bit mask, see PacketFlag. */
  uint32_t flags{0};
  /** Float parameter used by select sub-types. */
  float float_var{0.0F};
  /** Integer parameter, holds the variant call delay in milliseconds. */
  int32_t int_data{0};
  /** Primary position x. */
  float pos_x{0.0F};
  /** Primary position y. */
  float pos_y{0.0F};
  /** Secondary position x. */
  float pos_x2{0.0F};
  /** Secondary position y. */
  float pos_y2{0.0F};
  /** Particle timing parameter, unused by most sub-types. */
  uint32_t particle_time{0};
  /** Tile grid x for tile actions. */
  int32_t int_x{0};
  /** Tile grid y for tile actions. */
  int32_t int_y{0};
  /** Byte length of the extra data that follows the header. */
  uint32_t data_size{0};
};
#pragma pack(pop)

static_assert(sizeof(GameUpdatePacket) == 56,
              "GameUpdatePacket wire header must remain 56 bytes");

} // namespace spx::packet

#endif // SPXCORE_PACKET_PACKET_TYPE_HPP
