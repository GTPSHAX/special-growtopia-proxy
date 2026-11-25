#include "specialproxy/command/ChatCommandHandler.hpp"

#include <string_view>
#include <utility>

#include "specialproxy/net/Net.hpp"
#include "specialproxy/packet/Packet.hpp"
#include "specialproxy/proxy/ProxyService.hpp"
#include "specialproxy/utils/Logger.hpp"

namespace spx::command {

namespace {

constexpr std::string_view kActionPrefix{"action|"};
constexpr std::string_view kLegacyTextField{"|text|"};

/** @brief Remove a trailing carriage return from one chat line. */
auto trim_carriage_return(std::string text) -> std::string {
  if (!text.empty() && text.back() == '\r') {
    text.pop_back();
  }
  return text;
}

/** Reply channel that sends command frames to one game client. */
class ProxyCommandOutput final : public CommandOutput {
public:
  ProxyCommandOutput(proxy::ProxyService &service, proxy::Session &session)
      : service_{service}, session_{session} {}

  auto send_console(std::string_view message) -> bool override {
    return service_.send_to_client(
        session_, packet::builder::console_message_frame(message), true);
  }

  auto send_text(std::string_view message) -> bool override {
    return service_.send_to_client(session_,
                                   packet::builder::log_frame(message), true);
  }

private:
  proxy::ProxyService &service_;
  proxy::Session &session_;
};

} // namespace

ChatCommandHandler::ChatCommandHandler(CommandRegistry &registry,
                                       std::string version)
    : registry_{registry}, version_{std::move(version)} {}

auto ChatCommandHandler::registry() -> CommandRegistry & { return registry_; }

auto ChatCommandHandler::parse_chat_text(std::string_view body)
    -> std::optional<std::string> {
  const size_t first_end = body.find('\n');
  const auto first = body.substr(0, first_end);
  if (!first.starts_with(kActionPrefix)) {
    return std::nullopt;
  }
  auto action = first.substr(kActionPrefix.size());
  if (const size_t separator = action.find('|');
      separator != std::string_view::npos) {
    action = action.substr(0, separator);
  }
  if (action != "input") {
    return std::nullopt;
  }

  size_t cursor =
      first_end == std::string_view::npos ? body.size() : first_end + 1;
  while (cursor <= body.size()) {
    const size_t line_end = body.find('\n', cursor);
    const size_t stop =
        line_end == std::string_view::npos ? body.size() : line_end;
    const auto line = body.substr(cursor, stop - cursor);

    if (line.starts_with(kLegacyTextField)) {
      return trim_carriage_return(
          std::string{line.substr(kLegacyTextField.size())});
    }
    const size_t separator = line.find('|');
    if (separator != std::string_view::npos &&
        line.substr(0, separator) == "text") {
      return trim_carriage_return(std::string{line.substr(separator + 1)});
    }
    if (line_end == std::string_view::npos) {
      break;
    }
    cursor = line_end + 1;
  }
  return std::nullopt;
}

auto ChatCommandHandler::consume(proxy::ProxyService &service,
                                 proxy::Session &session,
                                 const net::Incoming &event) -> bool {
  if (event.type != net::EventType::Receive) {
    return false;
  }
  auto payload = packet::PacketDecoder::parse_payload(event.data);
  if (!payload) {
    return false;
  }
  const auto *text = packet::get_payload_if<packet::TextPayload>(*payload);
  if (text == nullptr) {
    return false;
  }
  auto input = parse_chat_text(text->data);
  if (!input || !registry_.is_command(*input)) {
    return false;
  }

  CommandEnvironment environment;
  environment.session_id = session.id;
  environment.server_address = session.server_address;
  environment.server_port = session.server_port;
  environment.client_count = service.session_count();
  environment.version = version_;
  environment.host_session = &session;
  ProxyCommandOutput output{service, session};
  const auto result = registry_.dispatch(*input, output, environment);
  if (result == CommandResult::UnknownCommand) {
    const auto name_end = input->find_first_of(" \t");
    const std::string_view name =
        name_end == std::string::npos
            ? std::string_view{*input}
            : std::string_view{*input}.substr(0, name_end);
    spx::log::infof("command",
                    "session %u unknown command %.*s forwarded upstream",
                    session.id, static_cast<int>(name.size()), name.data());
    return false;
  }

  spx::log::infof("command", "session %u chat input consumed", session.id);
  return true;
}

} // namespace spx::command
