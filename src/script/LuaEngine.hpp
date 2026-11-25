/** @file
 *  @brief Embedded Lua state and hook dispatch for proxy scripts.
 */

#ifndef SPXCORE_SCRIPT_LUA_ENGINE_HPP
#define SPXCORE_SCRIPT_LUA_ENGINE_HPP

#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "LuaPacket.hpp"
#include "specialproxy/net/Socks5.hpp"
#include "specialproxy/script/ScriptHost.hpp"

namespace spx::script {

/** Owns one Lua state and the hook registry bound to it. */
class LuaEngine {
public:
  LuaEngine();
  ~LuaEngine();

  LuaEngine(const LuaEngine &) = delete;
  auto operator=(const LuaEngine &) -> LuaEngine & = delete;

  /** @brief Compile and run one script chunk.
   *
   * @param[in] source Lua source code.
   * @param[in] name Label used in diagnostics.
   * @return True when the chunk loaded without errors.
   */
  auto load(std::string_view source, std::string_view name) -> bool;

  /** @brief Call the hook registered for one packet event.
   *
   * @param[in] event Canonical lowercase event name.
   * @param[in] view Decoded packet fields.
   * @param[in] host Host used by send functions during the call.
   * @return Decision applied by the proxy.
   */
  auto on_packet(std::string_view event, const LuaPacketView &view,
                 ScriptHost &host) -> HookReply;

  /** @brief Call a hook that carries only scalar arguments.
   *
   * @param[in] event Canonical lowercase event name.
   * @param[in] session_id Owning session id.
   * @param[in] arg1 First string argument.
   * @param[in] arg2 Second string argument.
   * @param[in] host Host used by send functions during the call.
   */
  void on_simple(std::string_view event, uint32_t session_id,
                 std::string_view arg1, std::string_view arg2,
                 ScriptHost &host);

  /** @brief Call the hook registered for inventory slot changes.
   *
   * @param[in] session_id Owning session id.
   * @param[in] changes Slot differences in ascending slot order.
   * @param[in] host Host used by send functions during the call.
   */
  void on_inventory(uint32_t session_id,
                    std::span<const InventoryChange> changes, ScriptHost &host);

  /** @brief Register a regex pattern whose dialogs are blocked.
   *
   * @param[in] pattern ECMAScript regular expression matched on dialog bodies.
   * @param[in] count Number of matches to block, negative blocks forever.
   * @return True when the pattern is valid or the count is zero.
   */
  auto add_dialog_block(std::string_view pattern, int32_t count) -> bool;

  /** @brief Consume one block for a dialog body matching a pattern.
   *
   * @param[in] dialog Dialog body carried by OnDialogRequest.
   * @return True when the dialog must be blocked.
   */
  auto consume_dialog_block(std::string_view dialog) -> bool;

  /** @brief Set the version reported by the version global.
   *
   * @param[in] value Version string.
   */
  void set_version(std::string_view value);

  /** @brief Apply the proxy SOCKS5 selection to script HTTP requests.
   *
   * Scripts can still override the selection with setHttpSocks5 afterwards.
   *
   * @param[in] config Proxy selection configured on the proxy core.
   */
  void inherit_socks5(const net::Socks5Config &config);

  /** @brief List commands registered from Lua scripts.
   *
   * @return Command names without the leading prefix.
   */
  [[nodiscard]] auto command_names() const -> std::vector<std::string>;

  /** @brief Consume a script reload request raised by Lua.
   *
   * @return True when Lua requested a reload since the last check.
   */
  auto take_reload_request() noexcept -> bool;

  /** @brief Invoke one Lua registered command.
   *
   * @param[in] name Command name without the prefix.
   * @param[in] args Arguments in declaration order.
   * @param[in] host Host used by send functions during the call.
   * @return True when the Lua callback completed without errors.
   */
  auto invoke_command(std::string_view name, std::span<const std::string> args,
                      ScriptHost &host) -> bool;

  /** @brief Test whether the underlying Lua state was created. */
  [[nodiscard]] auto available() const noexcept -> bool;

  /** @brief Advance deferred script calls and cap the next network wait.
   *
   * @param[in] timeout_ms Requested maximum wait.
   * @return Effective wait after accounting for pending wakeups.
   */
  auto poll_callback(uint32_t timeout_ms) -> uint32_t;

public:
  struct Impl; ///< Internal state shared with free helper functions.

private:
  std::unique_ptr<Impl> impl_;
};

} // namespace spx::script

#endif // SPXCORE_SCRIPT_LUA_ENGINE_HPP
