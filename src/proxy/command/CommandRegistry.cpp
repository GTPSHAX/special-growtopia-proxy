#include "specialproxy/command/CommandRegistry.hpp"

#include <string>

#include "specialproxy/utils/Logger.hpp"

namespace spx::command {

namespace {

/** @brief Stable text for one dispatch outcome. */
auto result_name(CommandResult result) -> const char * {
  switch (result) {
  case CommandResult::NotCommand:
    return "not_command";
  case CommandResult::UnknownCommand:
    return "unknown_command";
  case CommandResult::Success:
    return "success";
  case CommandResult::InvalidArguments:
    return "invalid_arguments";
  case CommandResult::Failed:
    return "failed";
  }
  return "failed";
}

} // namespace

void CommandRegistry::add(std::unique_ptr<ICommand> command) {
  if (command == nullptr) {
    return;
  }
  std::string key{command->name()};
  commands_[std::move(key)] = std::move(command);
}

auto CommandRegistry::remove(std::string_view name) -> bool {
  return commands_.erase(std::string{name}) != 0;
}

auto CommandRegistry::find(std::string_view name) const -> ICommand * {
  const auto it = commands_.find(std::string{name});
  return it == commands_.end() ? nullptr : it->second.get();
}

auto CommandRegistry::all() const
    -> std::vector<std::pair<std::string, std::string>> {
  std::vector<std::pair<std::string, std::string>> result;
  result.reserve(commands_.size());
  for (const auto &[name, command] : commands_) {
    result.emplace_back(name, command->description());
  }
  return result;
}

auto CommandRegistry::dispatch(std::string_view input, CommandOutput &output,
                               const CommandEnvironment &environment) const
    -> CommandResult {
  if (!parser_.is_command(input)) {
    return CommandResult::NotCommand;
  }

  auto parsed = parser_.parse(input);
  if (!parsed) {
    spx::log::warningf("command", "malformed input of %zu bytes", input.size());
    return CommandResult::InvalidArguments;
  }

  auto *command = find(parsed->name);
  if (command == nullptr) {
    spx::log::infof("command", "unknown command %.*s",
                    static_cast<int>(parsed->name.size()), parsed->name.data());
    return CommandResult::UnknownCommand;
  }

  CommandEnvironment extended = environment;
  extended.registry = this;
  const CommandContext context{parsed->name, parsed->args, input, output,
                               extended};
  const auto result = command->execute(context);
  spx::log::infof("command", "session %u command %.*s args=%zu result=%s",
                  environment.session_id, static_cast<int>(parsed->name.size()),
                  parsed->name.data(), parsed->args.size(),
                  result_name(result));
  return result;
}

} // namespace spx::command
