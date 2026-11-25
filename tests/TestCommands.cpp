#include <cassert>
#include <string>
#include <string_view>
#include <vector>

#include "specialproxy/command/Command.hpp"

using namespace spx::command;

namespace {

/** Captures command replies without a network host. */
class RecordingOutput final : public CommandOutput {
public:
  auto send_console(std::string_view message) -> bool override {
    console.emplace_back(message);
    return true;
  }

  auto send_text(std::string_view message) -> bool override {
    text.emplace_back(message);
    return true;
  }

  std::vector<std::string> console;
  std::vector<std::string> text;
};

auto test_parser() -> void {
  CommandParser parser;
  assert(parser.prefix() == '/');
  assert(parser.is_command("/help"));
  assert(!parser.is_command("hello"));
  assert(!parser.is_command(""));

  const auto parsed = parser.parse("  /say hello  world");
  assert(parsed.has_value());
  assert(parsed->name == "say");
  assert(parsed->args.size() == 2);
  assert(parsed->args[0] == "hello");
  assert(parsed->args[1] == "world");

  const auto quoted = parser.parse("/say \"hello world\" 42");
  assert(quoted.has_value());
  assert(quoted->args.size() == 2);
  assert(quoted->args[0] == "hello world");
  assert(quoted->args[1] == "42");

  assert(!parser.parse("/").has_value());
  assert(!parser.parse("/say \"unterminated").has_value());
  assert(!parser.parse("plain text").has_value());

  parser.set_prefix('!');
  assert(parser.is_command("!help"));
  assert(!parser.is_command("/help"));
}

auto test_registry_dispatch() -> void {
  CommandRegistry registry;
  registry.add("greet", "Greet one player", [](const CommandContext &context) {
    if (context.args().empty()) {
      return CommandResult::InvalidArguments;
    }
    context.output().send_text("Hello " + context.args().front());
    return CommandResult::Success;
  });

  RecordingOutput output;
  CommandEnvironment environment;
  environment.session_id = 7;

  assert(registry.dispatch("/greet Bob", output, environment) ==
         CommandResult::Success);
  assert(output.text.size() == 1);
  assert(output.text[0] == "Hello Bob");

  assert(registry.dispatch("ordinary chat", output, environment) ==
         CommandResult::NotCommand);
  assert(registry.dispatch("/missing", output, environment) ==
         CommandResult::UnknownCommand);
  assert(registry.dispatch("/", output, environment) ==
         CommandResult::InvalidArguments);
  assert(registry.dispatch("/greet", output, environment) ==
         CommandResult::InvalidArguments);
  assert(is_command_input(CommandResult::NotCommand) == false);
  assert(is_command_input(CommandResult::UnknownCommand) == true);
}

auto test_builtin_commands() -> void {
  CommandRegistry registry;
  register_builtin_commands(registry);
  assert(registry.size() == 5u);
  assert(registry.find("ping") != nullptr);
  assert(registry.find("absent") == nullptr);

  RecordingOutput output;
  CommandEnvironment environment;
  environment.session_id = 3;
  environment.server_address = "1.2.3.4";
  environment.server_port = 17044;
  environment.client_count = 2;
  environment.version = "0.1.0";

  assert(registry.dispatch("/ping", output, environment) ==
         CommandResult::Success);
  assert(output.console.back() == "pong");

  assert(registry.dispatch("/upstream", output, environment) ==
         CommandResult::Success);
  assert(output.console.back() == "1.2.3.4:17044");

  assert(registry.dispatch("/clients", output, environment) ==
         CommandResult::Success);
  assert(output.console.back() == "2 active sessions");

  assert(registry.dispatch("/version", output, environment) ==
         CommandResult::Success);
  assert(output.console.back() == "0.1.0");

  assert(registry.dispatch("/help", output, environment) ==
         CommandResult::Success);
  assert(output.console.back().find("/ping - ") != std::string::npos);
  assert(output.console.back().find("/upstream - ") != std::string::npos);
}

auto test_chat_text() -> void {
  const auto standard =
      ChatCommandHandler::parse_chat_text("action|input\ntext|/ping");
  assert(standard.has_value() && *standard == "/ping");

  const auto legacy =
      ChatCommandHandler::parse_chat_text("action|input\n|text|/help");
  assert(legacy.has_value() && *legacy == "/help");

  const auto with_return =
      ChatCommandHandler::parse_chat_text("action|input\ntext|/ping\r");
  assert(with_return.has_value() && *with_return == "/ping");

  assert(
      !ChatCommandHandler::parse_chat_text("action|log\nmsg|hi").has_value());
  assert(!ChatCommandHandler::parse_chat_text("garbage").has_value());
  assert(!ChatCommandHandler::parse_chat_text("action|input").has_value());
}

} // namespace

auto main() -> int {
  test_parser();
  test_registry_dispatch();
  test_builtin_commands();
  test_chat_text();
  return 0;
}
