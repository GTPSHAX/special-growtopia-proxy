#include "specialproxy/command/BuiltinCommands.hpp"

#include <string>

namespace spx::command {

void register_builtin_commands(CommandRegistry &registry) {
  registry.add("help", "List available chat commands",
               [](const CommandContext &context) {
                 const auto *source = context.environment().registry;
                 if (source == nullptr) {
                   return CommandResult::Failed;
                 }
                 const auto commands = source->all();
                 std::string text;
                 text.reserve(commands.size() * 32u);
                 for (const auto &[name, description] : commands) {
                   text.push_back('/');
                   text.append(name);
                   text.append(" - ");
                   text.append(description);
                   text.push_back('\n');
                 }
                 if (!text.empty()) {
                   text.pop_back();
                 }
                 context.output().send_console(text);
                 return CommandResult::Success;
               });

  registry.add("ping", "Check that the command layer is alive",
               [](const CommandContext &context) {
                 context.output().send_console("pong");
                 return CommandResult::Success;
               });

  registry.add("upstream", "Show the upstream target of this session",
               [](const CommandContext &context) {
                 const auto &environment = context.environment();
                 if (environment.server_address.empty() ||
                     environment.server_port == 0) {
                   context.output().send_console("upstream not discovered");
                   return CommandResult::Success;
                 }
                 std::string text{environment.server_address};
                 text.push_back(':');
                 text.append(std::to_string(environment.server_port));
                 context.output().send_console(text);
                 return CommandResult::Success;
               });

  registry.add("clients", "Show the number of connected game clients",
               [](const CommandContext &context) {
                 context.output().send_console(
                     std::to_string(context.environment().client_count) +
                     " active sessions");
                 return CommandResult::Success;
               });

  registry.add("version", "Show the running library version",
               [](const CommandContext &context) {
                 const auto version = context.environment().version;
                 context.output().send_console(
                     version.empty() ? "unknown" : std::string{version});
                 return CommandResult::Success;
               });
}

} // namespace spx::command
