/** @file
 *  @brief Host interface supplied by the proxy to the script engine.
 */

#ifndef SPXCORE_SCRIPT_SCRIPT_HOST_HPP
#define SPXCORE_SCRIPT_SCRIPT_HOST_HPP

#include <cstddef>
#include <cstdint>
#include <memory>
#include <span>
#include <string_view>
#include <vector>

namespace spx::script {

/** Decision returned by a packet hook to the proxy. */
enum class HookDecision {
  /** Keep the original frame and continue forwarding. */
  Forward,
  /** Stop forwarding and discard the frame. */
  Drop,
  /** Replace the frame with the bytes returned by the hook. */
  Replaced,
};

/** Result of one packet hook call. */
struct HookReply {
  HookDecision decision{HookDecision::Forward}; ///< What the proxy should do next.
  std::vector<std::byte> frame;                 ///< Replacement frame when replaced.
};

/** One slot difference reported by an inventory update. */
struct InventoryChange {
  uint32_t slot{0};       ///< One based index in the reported slot list.
  uint32_t old_id{0};     ///< Item id before the update.
  uint32_t old_count{0};  ///< Stack count before the update.
  uint32_t new_id{0};     ///< Item id after the update.
  uint32_t new_count{0};  ///< Stack count after the update.
};

/** @brief Capabilities a proxy session exposes to scripts. */
class ScriptHost {
 public:
  virtual ~ScriptHost() = default;

  /** @brief Send a frame to the game client of the active session.
   *
   * @param[in] frame Complete wire bytes.
   * @param[in] reliable Whether ENet delivery is reliable.
   * @return True when the frame was queued.
   */
  virtual auto send_to_client(std::span<const std::byte> frame,
                              bool reliable) -> bool = 0;

  /** @brief Send a frame to the upstream server of the active session.
   *
   * @param[in] frame Complete wire bytes.
   * @param[in] reliable Whether ENet delivery is reliable.
   * @return True when the frame was queued.
   */
  virtual auto send_to_server(std::span<const std::byte> frame,
                              bool reliable) -> bool = 0;

  /** @brief Read the identifier of the active session.
   *
   * @return Monotonic session number.
   */
  [[nodiscard]] virtual auto session_id() const -> uint32_t = 0;

  /** @brief Read the upstream address of the active session.
   *
   * @return Address advertised to this session.
   */
  [[nodiscard]] virtual auto server_address() const -> std::string_view = 0;

  /** @brief Read the upstream port of the active session.
   *
   * @return Port advertised to this session.
   */
  [[nodiscard]] virtual auto server_port() const -> uint16_t = 0;

  /** @brief Read the current number of game clients.
   *
   * @return Connected client count.
   */
  [[nodiscard]] virtual auto client_count() const -> size_t = 0;

  /** @brief Make a host copy that outlives a suspended script call.
   *
   * Hosts that support cooperative sleep return an independent copy. Hosts
   * that do not support it return null and the sleep falls back to blocking.
   *
   * @return Owned host copy, or null when not supported.
   */
  virtual auto clone() -> std::shared_ptr<ScriptHost> { return nullptr; }
};

} // namespace spx::script

#endif // SPXCORE_SCRIPT_SCRIPT_HOST_HPP
