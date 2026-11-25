#include <cassert>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

#include "PlatformError.hpp"
#include "specialproxy/platforms/detail/HostsBlock.hpp"

using namespace spx::platform;

namespace {

auto test_add_to_empty() -> void {
  auto split = detail::split_block("");
  auto entries = detail::parse_entries(split.body);
  entries.push_back(HostEntry{"growtopia.com", "127.0.0.1"});
  const std::string content = detail::join(split, entries);

  assert(content.find(detail::kBlockBegin) != std::string::npos);
  assert(content.find("127.0.0.1 growtopia.com") != std::string::npos);

  const auto roundtrip = detail::split_block(content);
  const auto listed = detail::parse_entries(roundtrip.body);
  assert(listed.size() == 1);
  assert(listed[0].hostname == "growtopia.com");
  assert(listed[0].ip == "127.0.0.1");
}

auto test_idempotent_add() -> void {
  const std::string content = std::string{detail::kBlockBegin} +
                              "\n127.0.0.1 growtopia.com\n" +
                              std::string{detail::kBlockEnd} + "\n";
  auto split = detail::split_block(content);
  auto entries = detail::parse_entries(split.body);
  for (auto &entry : entries) {
    if (entry.hostname == "growtopia.com") {
      entry.ip = "127.0.0.1";
    }
  }
  const std::string again = detail::join(split, entries);
  assert(again == content);
}

auto test_update_existing_host() -> void {
  const std::string content = std::string{detail::kBlockBegin} +
                              "\n127.0.0.1 growtopia.com\n" +
                              std::string{detail::kBlockEnd} + "\n";
  auto split = detail::split_block(content);
  auto entries = detail::parse_entries(split.body);
  for (auto &entry : entries) {
    if (entry.hostname == "growtopia.com") {
      entry.ip = "10.0.0.5";
    }
  }
  const std::string updated = detail::join(split, entries);
  assert(updated.find("10.0.0.5 growtopia.com") != std::string::npos);
  assert(updated.find("127.0.0.1 growtopia.com") == std::string::npos);
  assert(detail::parse_entries(detail::split_block(updated).body).size() == 1);
}

auto test_outside_content_preserved() -> void {
  const std::string content =
      "127.0.0.1 localhost\n" + std::string{detail::kBlockBegin} +
      "\n127.0.0.1 growtopia.com\n" + std::string{detail::kBlockEnd} +
      "\n# trailing comment\n";
  auto split = detail::split_block(content);
  const std::string removed = split.before + split.after;

  assert(removed.find("127.0.0.1 localhost") != std::string::npos);
  assert(removed.find("# trailing comment") != std::string::npos);
  assert(removed.find(detail::kBlockBegin) == std::string::npos);
  assert(removed.find("growtopia.com") == std::string::npos);
}

auto test_malformed_lines_skipped() -> void {
  const std::string content = std::string{detail::kBlockBegin} +
                              "\ngarbage\n\n10.0.0.1 host.one\n" +
                              std::string{detail::kBlockEnd} + "\n";
  const auto listed = detail::parse_entries(detail::split_block(content).body);
  assert(listed.size() == 1);
  assert(listed[0].hostname == "host.one");
  assert(listed[0].ip == "10.0.0.1");
}

auto test_external_conflict_override_and_restore() -> void {
  const std::string content =
      "# 103.126.117.167 www.growtopia1.com # MafiaPS\n"
      "202.155.143.176 www.growtopia1.com # Lowping Lucky\n"
      "202.155.143.176 login.growtopiagame.com\n"
      "127.0.0.1 localhost\n"
      "\n"
      "# BEGIN specialproxy\n"
      "127.0.0.1 login.growtopiagame.com\n"
      "# END specialproxy\n";
  const std::vector<std::string_view> names{"login.growtopiagame.com",
                                            "www.growtopia1.com"};
  const auto disabled = detail::disable_conflicts(content, names);
  assert(disabled.originals.size() == 2);
  assert(disabled.originals[0] ==
         "202.155.143.176 www.growtopia1.com # Lowping Lucky");
  assert(disabled.originals[1] == "202.155.143.176 login.growtopiagame.com");

  auto split = detail::split_block(disabled.content);
  auto data = detail::parse_block(split.body);
  data.entries.clear();
  for (const auto name : names) {
    data.entries.push_back(HostEntry{std::string{name}, "127.0.0.1"});
  }
  data.disabled = disabled.originals;
  const std::string installed = detail::join(split, data);

  assert(installed.find(std::string{detail::kDisabledMarker} +
                        "202.155.143.176 www.growtopia1.com") !=
         std::string::npos);
  assert(installed.find("\n202.155.143.176 login.growtopiagame.com\n") ==
         std::string::npos);
  const auto listed =
      detail::parse_entries(detail::split_block(installed).body);
  assert(listed.size() == 2);
  assert(listed[0].hostname == "login.growtopiagame.com");
  assert(listed[1].hostname == "www.growtopia1.com");

  const auto restored_split = detail::split_block(installed);
  const auto restored = detail::restore_block(
      restored_split, detail::parse_block(restored_split.body));
  assert(restored.find(std::string{detail::kDisabledMarker}) ==
         std::string::npos);
  assert(restored.find("202.155.143.176 www.growtopia1.com # Lowping Lucky") !=
         std::string::npos);
  assert(restored.find("202.155.143.176 login.growtopiagame.com") !=
         std::string::npos);
  assert(restored.find("127.0.0.1 localhost") != std::string::npos);
  assert(restored.find("127.0.0.1 login.growtopiagame.com") ==
         std::string::npos);
}

auto test_case_tabs_and_comments() -> void {
  const std::string content =
      "\t# 202.155.143.176 www.growtopia1.com # old comment\n"
      "202.155.143.176\tWWW.Growtopia1.com\t# Lowping Lucky\n"
      "  ::1 login.growtopiagame.com\n";
  const std::vector<std::string_view> names{"www.growtopia1.com",
                                            "login.growtopiagame.com"};
  const auto disabled = detail::disable_conflicts(content, names);
  assert(disabled.originals.size() == 2);
  assert(disabled.content.find("# old comment") != std::string::npos);
  assert(disabled.content.find("202.155.143.176\tWWW.Growtopia1.com") ==
         std::string::npos);
}

auto test_crlf_preserved() -> void {
  const std::string content = "202.155.143.176 www.growtopia1.com\r\n"
                              "# BEGIN specialproxy\r\n"
                              "127.0.0.1 login.growtopiagame.com\r\n"
                              "# END specialproxy\r\n";
  const std::vector<std::string_view> names{"www.growtopia1.com"};
  const auto disabled = detail::disable_conflicts(content, names);
  const auto split = detail::split_block(disabled.content);
  assert(split.newline == "\r\n");
  auto data = detail::parse_block(split.body);
  data.disabled = disabled.originals;
  const std::string installed = detail::join(split, data);
  assert(installed.find("\r\n") != std::string::npos);
  for (size_t i = 0; i < installed.size(); ++i) {
    if (installed[i] == '\n') {
      assert(i > 0 && installed[i - 1] == '\r');
    }
  }
}

auto test_repeat_install_is_clean() -> void {
  const std::string content =
      std::string{detail::kBlockBegin} +
      "\n# spx-disabled 202.155.143.176 www.growtopia1.com # Lowping Lucky\n"
      "127.0.0.1 login.growtopiagame.com\n"
      "127.0.0.1 www.growtopia1.com\n" +
      std::string{detail::kBlockEnd} + "\n";
  const std::vector<std::string_view> names{"login.growtopiagame.com",
                                            "www.growtopia1.com"};
  const auto disabled = detail::disable_conflicts(content, names);
  assert(disabled.originals.empty());
  assert(disabled.content == content);
}

/** @brief Exercise the real file layer against the reported hosts content. */
auto test_real_hosts_write() -> void {
  const auto directory =
      std::filesystem::temp_directory_path() / "specialproxy-hosts-test";
  std::filesystem::create_directories(directory);
  const auto path = directory / "hosts";
  const std::string sample =
      "# localhost name resolution is handled within DNS itself.\n"
      "#\t127.0.0.1       localhost\n"
      "#\t::1             localhost\n"
      "\n"
      "# 103.126.117.167 www.growtopia1.com # MafiaPS\n"
      "202.155.143.176 www.growtopia1.com # Lowping Lucky\n"
      "202.155.143.176 login.growtopiagame.com\n"
      "\n"
      "# BEGIN specialproxy\n"
      "127.0.0.1 login.growtopiagame.com\n"
      "# END specialproxy\n";
  {
    std::ofstream out{path, std::ios::binary | std::ios::trunc};
    out << sample;
    assert(out.good());
  }

  detail::set_hosts_path_override(path.string());
  const std::vector<HostEntry> requested{
      HostEntry{"www.growtopia1.com", "127.0.0.1"},
      HostEntry{"login.growtopiagame.com", "127.0.0.1"}};
  assert(add_host_entries(requested));

  std::ifstream in{path, std::ios::binary};
  std::ostringstream stream;
  stream << in.rdbuf();
  const std::string written = stream.str();
  assert(written.find("127.0.0.1 www.growtopia1.com\n") != std::string::npos);
  assert(written.find("127.0.0.1 login.growtopiagame.com\n") !=
         std::string::npos);
  assert(written.find("\n202.155.143.176 www.growtopia1.com # Lowping "
                      "Lucky\n") == std::string::npos);
  const auto listed = list_host_entries();
  assert(listed.size() == 2);

  assert(remove_host_entries());
  in.clear();
  in.seekg(0);
  std::ostringstream restored_stream;
  restored_stream << in.rdbuf();
  const std::string restored = restored_stream.str();
  assert(restored.find("# spx-disabled") == std::string::npos);
  assert(restored.find("202.155.143.176 www.growtopia1.com # Lowping "
                       "Lucky") != std::string::npos);
  assert(restored.find("202.155.143.176 login.growtopiagame.com") !=
         std::string::npos);
  assert(restored.find("127.0.0.1 www.growtopia1.com") == std::string::npos);

  detail::set_hosts_path_override({});
  std::remove(path.string().c_str());
}

auto test_device_info() -> void {
  const auto info = device_info();
  assert(!info.hostname.empty());
  assert(!info.os.empty());
  assert(info.platform == "windows" || info.platform == "linux" ||
         info.platform == "macos" || info.platform == "android");
  assert(info.arch == "x86" || info.arch == "x64" || info.arch == "arm" ||
         info.arch == "arm64");
  assert(info.cpu_count > 0);
}

} // namespace

int main() {
  test_device_info();
  test_add_to_empty();
  test_idempotent_add();
  test_update_existing_host();
  test_outside_content_preserved();
  test_malformed_lines_skipped();
  test_external_conflict_override_and_restore();
  test_case_tabs_and_comments();
  test_crlf_preserved();
  test_repeat_install_is_clean();
  test_real_hosts_write();
  return 0;
}
