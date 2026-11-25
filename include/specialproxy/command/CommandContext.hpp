/** @file
 *  @brief Per-invocation data handed to a command implementation.
 */

#ifndef SPXCORE_COMMAND_COMMAND_CONTEXT_HPP
#define SPXCORE_COMMAND_COMMAND_CONTEXT_HPP

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#include "specialproxy/command/CommandOutput.hpp"

namespace spx::command {

class CommandRegistry;

/** Static facts about the proxy session that triggered a command. */
struct CommandEnvironment {
  uint32_t session_id{0};            ///< Identifier of the requesting session.
  std::string_view server_address;   ///< Upstream target of the session.
  uint16_t server_port{0};           ///< Upstream port of the session.
  size_t client_count{0};            ///< Number of connected game clients.
  std::string_view version;          ///< Library version string.
  const CommandRegistry* registry{nullptr}; ///< Registry used for this dispatch.
  void* host_session{nullptr};       ///< Opaque session pointer from the host.
};

/** @brief Read-only view of one command invocation. */
class CommandContext {
 public:
  /** @brief Bind a parsed command to its output and environment.
   *
   * @param[in] name Command name without the prefix.
   * @param[in] args Command arguments in declaration order.
   * @param[in] raw_input Original chat input including the prefix.
   * @param[in] output Channel used to reply to the client.
   * @param[in] environment Facts about the triggering session.
   */
  CommandContext(std::string_view name, std::span<const std::string> args,
                 std::string_view raw_input, CommandOutput& output,
                 const CommandEnvironment& environment) noexcept
      : name_{name},
        args_{args},
        raw_input_{raw_input},
        output_{output},
        environment_{environment} {}

  /** @brief Read the command name.
   *
   * @return Command name without the prefix.
   */
  [[nodiscard]] auto name() const noexcept -> std::string_view {
    return name_;
  }

  /** @brief Read the command arguments.
   *
   * @return Arguments in declaration order.
   */
  [[nodiscard]] auto args() const noexcept -> std::span<const std::string> {
    return args_;
  }

  /** @brief Read the original chat input.
   *
   * @return Raw input including the prefix.
   */
  [[nodiscard]] auto raw_input() const noexcept -> std::string_view {
    return raw_input_;
  }

  /** @brief Access the reply channel.
   *
   * @return Output bound to the requesting client.
   */
  [[nodiscard]] auto output() const noexcept -> CommandOutput& {
    return output_;
  }

  /** @brief Access facts about the triggering session.
   *
   * @return Environment captured at dispatch time.
   */
  [[nodiscard]] auto environment() const noexcept
      -> const CommandEnvironment& {
    return environment_;
  }

 private:
  std::string_view name_;
  std::span<const std::string> args_;
  std::string_view raw_input_;
  CommandOutput& output_;
  const CommandEnvironment& environment_;
};

} // namespace spx::command

#endif // SPXCORE_COMMAND_COMMAND_CONTEXT_HPP
