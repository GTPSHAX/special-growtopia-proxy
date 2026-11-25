/** @file
 *  @brief Interactive client for exercising the core end to end on Windows.
 *
 *  Connects to 127.0.0.1:17099, sends every stdin line as a text packet and
 *  prints decoded replies. Pair with CoreTestServer.
 */

#include <atomic>
#include <cstdio>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "specialproxy/net/Net.hpp"
#include "specialproxy/packet/Packet.hpp"

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

using namespace spx::packet;

constexpr uint16_t kServerPort = 17099;

std::atomic_bool g_running{true};
std::mutex g_lines_mutex;
std::vector<std::string> g_lines;

#ifdef _WIN32
BOOL WINAPI handle_ctrl(DWORD type) {
  (void)type;
  g_running = false;
  return TRUE;
}
#endif

/** @brief Expand literal backslash n sequences into newlines. */
auto expand_newlines(const std::string& text) -> std::string {
  std::string out;
  out.reserve(text.size());
  for (size_t i = 0; i < text.size(); ++i) {
    if (text[i] == '\\' && i + 1 < text.size() && text[i + 1] == 'n') {
      out.push_back('\n');
      ++i;
    } else {
      out.push_back(text[i]);
    }
  }
  return out;
}

/** @brief Human readable summary of a decoded payload. */
void print_payload(const Payload& payload) {
  if (const auto* text = get_payload_if<TextPayload>(payload)) {
    std::printf("  text type %u body %s\n", text->message_type,
                text->data.c_str());
  } else if (const auto* variant = get_payload_if<VariantPayload>(payload)) {
    const auto* name = variant->list.get<std::string>(0);
    const auto* message = variant->list.get<std::string>(1);
    std::printf("  variant %s %s\n",
                name != nullptr ? name->c_str() : "<unnamed>",
                message != nullptr ? message->c_str() : "");
    if (name != nullptr && *name == "OnSendToServer") {
      const auto* port = variant->list.get<uint32_t>(1);
      const auto* combined = variant->list.get<std::string>(4);
      std::printf("  port %u target %s\n",
                  port != nullptr ? static_cast<unsigned>(*port) : 0u,
                  combined != nullptr ? combined->c_str() : "");
    }
  }
}

} // namespace

int main() {
#ifdef _WIN32
  SetConsoleOutputCP(CP_UTF8);
  SetConsoleCtrlHandler(handle_ctrl, TRUE);
  setvbuf(stdout, nullptr, _IONBF, 0);
#endif

  if (!spx::net::initialize()) {
    std::printf("enet initialize failed\n");
    return 1;
  }

  auto host = spx::net::Host::create(0, 1, spx::net::HostRole::Client);
  if (!host) {
    std::printf("client host failed\n");
    spx::net::deinitialize();
    return 1;
  }

  PacketRegistry::instance().register_packet<game::OnConsoleMessage>();

  ENetPeer* peer = host->connect("127.0.0.1", kServerPort);
  if (peer == nullptr) {
    std::printf("connect request failed\n");
    spx::net::deinitialize();
    return 1;
  }
  std::printf("connecting to 127.0.0.1:%u, type lines to send, quit exits\n",
              kServerPort);

  std::thread console{[] {
    std::string line;
    while (g_running && std::getline(std::cin, line)) {
      if (line == "quit") {
        g_running = false;
        break;
      }
      std::lock_guard<std::mutex> lock(g_lines_mutex);
      g_lines.push_back(line);
    }
  }};

  // Keep pending stdin lines so sends stay on the service thread.
  bool connected = false;
  while (g_running) {
    // Drained here so enet_host_send stays on the service thread.
    if (connected) {
      std::vector<std::string> lines;
      {
        std::lock_guard<std::mutex> lock(g_lines_mutex);
        lines = std::move(g_lines);
        g_lines.clear();
      }
      for (const auto& line : lines) {
        auto frame = serialize_payload(
            Payload{TextPayload{3u, expand_newlines(line)}});
        host->send(peer, frame, true);
      }
    }

    auto event = host->service(50);
    if (event) {
      switch (event->type) {
        case spx::net::EventType::Connect:
          std::printf("[connect] peer %u\n", event->peer->connectID);
          connected = true;
          break;
        case spx::net::EventType::Disconnect:
          std::printf("[disconnect] peer %u\n", event->peer->connectID);
          break;
        case spx::net::EventType::Receive: {
          auto payload = PacketDecoder::parse_payload(event->data);
          auto decoded = PacketDecoder::decode(event->data);
          if (!payload || !decoded) {
            std::printf("[recv] malformed %zu bytes\n", event->data.size());
            break;
          }
          std::printf("[recv] %s\n", packet_name((*decoded)->id()).data());
          print_payload(*payload);
          break;
        }
        case spx::net::EventType::None:
          break;
      }
    }
  }

  console.join();
  // Flush lines queued before quit so the server still sees them.
  std::vector<std::string> lines;
  {
    std::lock_guard<std::mutex> lock(g_lines_mutex);
    lines = std::move(g_lines);
    g_lines.clear();
  }
  if (connected) {
    for (const auto& line : lines) {
      auto frame = serialize_payload(
          Payload{TextPayload{3u, expand_newlines(line)}});
      host->send(peer, frame, true);
    }
    if (!lines.empty()) {
      host->service(200);
    }
  }
  std::printf("bye\n");
  spx::net::deinitialize();
  return 0;
}
