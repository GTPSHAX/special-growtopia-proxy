#include "specialproxy/command/CommandParser.hpp"

#include <algorithm>
#include <iterator>

namespace spx::command {

namespace {

constexpr std::string_view kWhitespace{" \t\r\n"};

/** @brief Skip whitespace and return the first content offset. */
auto skip_space(std::string_view text, size_t cursor) noexcept -> size_t {
  while (cursor < text.size() &&
         kWhitespace.find(text[cursor]) != std::string_view::npos) {
    ++cursor;
  }
  return cursor;
}

} // namespace

auto CommandParser::is_command(std::string_view input) const noexcept -> bool {
  const size_t start = skip_space(input, 0);
  return start < input.size() && input[start] == prefix_;
}

auto CommandParser::parse(std::string_view input) const
    -> std::optional<ParsedCommand> {
  const size_t first = skip_space(input, 0);
  if (first >= input.size() || input[first] != prefix_) {
    return std::nullopt;
  }

  std::vector<std::string> tokens;
  size_t cursor = first + 1;
  while (cursor < input.size()) {
    cursor = skip_space(input, cursor);
    if (cursor >= input.size()) {
      break;
    }

    std::string token;
    if (input[cursor] == '"') {
      ++cursor;
      bool closed = false;
      while (cursor < input.size()) {
        if (input[cursor] == '"') {
          ++cursor;
          closed = true;
          break;
        }
        token.push_back(input[cursor]);
        ++cursor;
      }
      if (!closed) {
        return std::nullopt;
      }
    } else {
      const size_t start = cursor;
      while (cursor < input.size() &&
             kWhitespace.find(input[cursor]) == std::string_view::npos) {
        ++cursor;
      }
      token.assign(input.substr(start, cursor - start));
    }
    tokens.push_back(std::move(token));
  }

  if (tokens.empty()) {
    return std::nullopt;
  }

  ParsedCommand parsed;
  parsed.name = std::move(tokens.front());
  parsed.args.assign(std::make_move_iterator(tokens.begin() + 1),
                     std::make_move_iterator(tokens.end()));
  return parsed;
}

} // namespace spx::command
