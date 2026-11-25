/** @file
 *  @brief Pure helpers for the managed hosts file block.
 */

#ifndef SPXCORE_PLATFORMS_DETAIL_HOSTS_BLOCK_HPP
#define SPXCORE_PLATFORMS_DETAIL_HOSTS_BLOCK_HPP

#include <cctype>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "specialproxy/platforms/Platform.hpp"

namespace spx::platform::detail {

/** Marker line opening the proxy managed block. */
inline constexpr std::string_view kBlockBegin{"# BEGIN specialproxy"};

/** Marker line closing the proxy managed block. */
inline constexpr std::string_view kBlockEnd{"# END specialproxy"};

/** Prefix storing an original line that the proxy had to disable. */
inline constexpr std::string_view kDisabledMarker{"# spx-disabled "};

/** @brief Newline style used by the file, kept when rewriting it. */
inline auto detect_newline(std::string_view content) -> std::string {
  return content.find("\r\n") != std::string_view::npos ? "\r\n" : "\n";
}

/** @brief Remove leading horizontal whitespace from a line. */
inline auto trim_left(std::string_view line) -> std::string_view {
  size_t at = 0;
  while (at < line.size() && (line[at] == ' ' || line[at] == '\t')) {
    ++at;
  }
  return line.substr(at);
}

/** @brief Visit each line of a block without its line ending. */
template <typename Fn>
void for_each_line(std::string_view text, Fn&& visit) {
  size_t cursor = 0;
  while (cursor < text.size()) {
    size_t end = text.find('\n', cursor);
    if (end == std::string_view::npos) {
      end = text.size();
    }
    std::string_view line = text.substr(cursor, end - cursor);
    if (!line.empty() && line.back() == '\r') {
      line.remove_suffix(1);
    }
    visit(line);
    cursor = end < text.size() ? end + 1 : text.size();
  }
}

/** @brief Read the address and the first hostname of a mapping line. */
inline auto first_two_tokens(std::string_view line, std::string_view& address,
                             std::string_view& hostname) -> bool {
  const std::string_view text = trim_left(line);
  if (text.empty() || text.front() == '#') {
    return false;
  }
  size_t at = 0;
  const size_t address_start = at;
  while (at < text.size() && text[at] != ' ' && text[at] != '\t') {
    ++at;
  }
  address = text.substr(address_start, at - address_start);
  while (at < text.size() && (text[at] == ' ' || text[at] == '\t')) {
    ++at;
  }
  if (at == text.size() || text[at] == '#') {
    return false;
  }
  const size_t hostname_start = at;
  while (at < text.size() && text[at] != ' ' && text[at] != '\t') {
    ++at;
  }
  hostname = text.substr(hostname_start, at - hostname_start);
  return !address.empty() && !hostname.empty();
}

/** @brief Case insensitive comparison of two hostnames. */
inline auto same_hostname(std::string_view left, std::string_view right)
    -> bool {
  if (left.size() != right.size()) {
    return false;
  }
  for (size_t i = 0; i < left.size(); ++i) {
    const auto a = std::tolower(static_cast<unsigned char>(left[i]));
    const auto b = std::tolower(static_cast<unsigned char>(right[i]));
    if (a != b) {
      return false;
    }
  }
  return true;
}

/** @brief Whether an active mapping line resolves the given hostname. */
inline auto maps_hostname(std::string_view line, std::string_view hostname)
    -> bool {
  std::string_view address;
  std::string_view name;
  return first_two_tokens(line, address, name) &&
         same_hostname(name, hostname);
}

/** Hosts file pieces around the managed block. */
struct HostsSplit {
  std::string before;        ///< Content ahead of the block.
  std::string body;          ///< Block lines without the markers.
  std::string after;         ///< Content behind the block.
  std::string newline{"\n"}; ///< Dominant newline style of the file.
  bool block_present{false}; ///< True when both markers were found.
};

/** @brief Split file content into outside parts and the block body. */
inline auto split_block(std::string_view content) -> HostsSplit {
  HostsSplit split;
  split.newline = detect_newline(content);
  const size_t begin = content.find(kBlockBegin);
  if (begin == std::string_view::npos) {
    split.before = std::string{content};
    return split;
  }
  split.block_present = true;
  split.before = std::string{content.substr(0, begin)};
  size_t cursor = begin + kBlockBegin.size();
  if (cursor < content.size() && content[cursor] == '\r') {
    ++cursor;
  }
  if (cursor < content.size() && content[cursor] == '\n') {
    ++cursor;
  }
  const size_t end = content.find(kBlockEnd, cursor);
  if (end == std::string_view::npos) {
    split.body = std::string{content.substr(cursor)};
    return split;
  }
  split.body = std::string{content.substr(cursor, end - cursor)};
  size_t after = end + kBlockEnd.size();
  if (after < content.size() && content[after] == '\r') {
    ++after;
  }
  if (after < content.size() && content[after] == '\n') {
    ++after;
  }
  split.after = std::string{content.substr(after)};
  return split;
}

/** @brief Parse "ip hostname" lines of a block body. */
inline auto parse_entries(std::string_view body) -> std::vector<HostEntry> {
  std::vector<HostEntry> entries;
  for_each_line(body, [&](std::string_view line) {
    if (line.starts_with(kDisabledMarker)) {
      return;
    }
    std::string_view address;
    std::string_view hostname;
    if (first_two_tokens(line, address, hostname)) {
      entries.push_back(HostEntry{std::string{hostname},
                                  std::string{address}});
    }
  });
  return entries;
}

/** Entries and saved original lines inside the managed block. */
struct BlockData {
  std::vector<HostEntry> entries;    ///< Active proxy mappings.
  std::vector<std::string> disabled; ///< Original lines to restore on stop.
};

/** @brief Parse the whole block body, including disabled originals. */
inline auto parse_block(std::string_view body) -> BlockData {
  BlockData data;
  for_each_line(body, [&](std::string_view line) {
    const std::string_view text = trim_left(line);
    if (text.starts_with(kDisabledMarker)) {
      data.disabled.emplace_back(text.substr(kDisabledMarker.size()));
      return;
    }
    std::string_view address;
    std::string_view hostname;
    if (first_two_tokens(line, address, hostname)) {
      data.entries.push_back(HostEntry{std::string{hostname},
                                       std::string{address}});
    }
  });
  return data;
}

/** @brief Serialize a full marked block with the chosen newline style. */
inline auto serialize_block(const BlockData& data, std::string_view newline)
    -> std::string {
  std::string block{kBlockBegin};
  block += newline;
  for (const auto& original : data.disabled) {
    block += kDisabledMarker;
    block += original;
    block += newline;
  }
  for (const auto& entry : data.entries) {
    block += entry.ip;
    block.push_back(' ');
    block += entry.hostname;
    block += newline;
  }
  block += kBlockEnd;
  block += newline;
  return block;
}

/** @brief Serialize active entries with the default newline style. */
inline auto serialize_block(const std::vector<HostEntry>& entries)
    -> std::string {
  return serialize_block(BlockData{entries, {}}, "\n");
}

/** @brief Ensure a string ends with exactly one newline separator. */
inline void append_line_feed(std::string& text, std::string_view newline = "\n") {
  if (text.empty() || text.back() == '\n') {
    return;
  }
  if (text.back() == '\r') {
    text.push_back('\n');
    return;
  }
  text += newline;
}

/** @brief Join outside content and a rebuilt block into file content. */
inline auto join(const HostsSplit& split, const BlockData& data)
    -> std::string {
  std::string result = split.before;
  append_line_feed(result, split.newline);
  result += serialize_block(data, split.newline);
  result += split.after;
  return result;
}

/** @brief Join outside content and active entries into file content. */
inline auto join(const HostsSplit& split,
                 const std::vector<HostEntry>& entries) -> std::string {
  return join(split, BlockData{entries, {}});
}

/** Result of disabling conflicting lines outside the managed block. */
struct DisabledResult {
  std::string content;                ///< File content without the conflicts.
  std::vector<std::string> originals; ///< Original line texts, in file order.
};

/** @brief Comment out active mappings that would override our entries. */
inline auto disable_conflicts(std::string_view content,
                              const std::vector<std::string_view>& hostnames)
    -> DisabledResult {
  DisabledResult result;
  bool inside = false;
  size_t cursor = 0;
  while (cursor < content.size()) {
    size_t end = content.find('\n', cursor);
    if (end == std::string_view::npos) {
      end = content.size();
    }
    const size_t raw_end = end < content.size() ? end + 1 : end;
    const std::string_view raw = content.substr(cursor, raw_end - cursor);
    std::string_view line = content.substr(cursor, end - cursor);
    if (!line.empty() && line.back() == '\r') {
      line.remove_suffix(1);
    }
    const std::string_view text = trim_left(line);
    if (text.starts_with(kBlockBegin)) {
      inside = true;
    } else if (text.starts_with(kBlockEnd)) {
      inside = false;
    }
    bool conflicting = false;
    if (!inside && !text.starts_with(kDisabledMarker) &&
        !text.empty() && text.front() != '#') {
      for (const auto hostname : hostnames) {
        if (maps_hostname(text, hostname)) {
          conflicting = true;
          break;
        }
      }
    }
    if (conflicting) {
      result.originals.emplace_back(line);
    } else {
      result.content += raw;
    }
    cursor = raw_end;
  }
  return result;
}

/** @brief Rebuild file content without the block and with originals back. */
inline auto restore_block(const HostsSplit& split, const BlockData& data)
    -> std::string {
  std::string result = split.before;
  append_line_feed(result, split.newline);
  for (const auto& original : data.disabled) {
    result += original;
    result += split.newline;
  }
  result += split.after;
  return result;
}

} // namespace spx::platform::detail

#endif // SPXCORE_PLATFORMS_DETAIL_HOSTS_BLOCK_HPP
