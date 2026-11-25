/** @file
 *  @brief Bridge between game chat input and the command registry.
 */

#ifndef SPXCORE_COMMAND_CHAT_COMMAND_HANDLER_HPP
#define SPXCORE_COMMAND_CHAT_COMMAND_HANDLER_HPP

#include <optional>
#include <string>
#include <string_view>

#include "specialproxy/command/CommandRegistry.hpp"

namespace spx::net {
struct Incoming;
}

namespace spx::proxy {
class ProxyService;
struct Session;
} // namespace spx::proxy

namespace spx::command {

/** Intercepts prefixed game chat and routes it to a command registry. */
class ChatCommandHandler {
 public:
  /** @brief Bind a handler to one registry and version string.
   *
   * @param[in] registry Registry used for every dispatch.
   * @param[in] version Library version reported by the version command.
   */
  ChatCommandHandler(CommandRegistry& registry, std::string version);

  /** @brief Try to handle one incoming packet as a chat command.
   *
   * @param[in] service Proxy service used to send command replies.
   * @param[in] session Session that received the packet.
   * @param[in] event Incoming network event.
   * @return True when the input was consumed and must not be forwarded.
   */
  auto consume(proxy::ProxyService& service, proxy::Session& session,
               const net::Incoming& event) -> bool;

  /** @brief Access the registry used by this handler.
   *
   * @return Mutable registry for adding commands before the proxy starts.
   */
  [[nodiscard]] auto registry() -> CommandRegistry&;

  /** @brief Extract the chat text of a game input frame body.
   *
   * @param[in] body Text payload body without the frame prefix.
   * @return Input text, or nullopt for other actions and malformed bodies.
   */
  [[nodiscard]] static auto parse_chat_text(std::string_view body)
      -> std::optional<std::string>;

 private:
  CommandRegistry& registry_;
  std::string version_;
};

} // namespace spx::command

#endif // SPXCORE_COMMAND_CHAT_COMMAND_HANDLER_HPP
