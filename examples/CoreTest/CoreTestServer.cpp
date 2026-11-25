/** @file
 *  @brief Interactive server for exercising the core end to end on Windows.
 *
 *  Listens on UDP 17099, decodes every incoming frame through the packet
 *  module, prints it and answers with an OnConsoleMessage variant. Console
 *  commands control the run without a debugger.
 */

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "specialproxy/net/Net.hpp"
#include "specialproxy/packet/Packet.hpp"
#include "specialproxy/proxy/Core.hpp"

extern "C" {
#include <zlib.h>
}

#ifdef _WIN32
#include <windows.h>
#endif

namespace {

using namespace spx::packet;

constexpr uint16_t kListenPort = 17091;

std::atomic_bool g_running{true};
std::mutex g_peers_mutex;
std::vector<ENetPeer*> g_peers;

#ifdef _WIN32
BOOL WINAPI handle_ctrl(DWORD type) {
  (void)type;
  g_running = false;
  return TRUE;
}
#endif

/** @brief Human readable summary of a decoded payload. */
void print_payload(const Payload& payload) {
  if (const auto* text = get_payload_if<TextPayload>(payload)) {
    std::printf("  text type %u len %zu\n", text->message_type,
                text->data.size());
  } else if (const auto* game = get_payload_if<GamePayload>(payload)) {
    std::printf("  tank type %u net_id %u extra %zu\n", game->packet.type,
                game->packet.net_id, game->extra.size());
  } else if (const auto* variant = get_payload_if<VariantPayload>(payload)) {
    const auto* name = variant->list.get<std::string>(0);
    std::printf("  variant %s params %zu\n",
                name != nullptr ? name->c_str() : "<unnamed>",
                variant->list.size());
  } else if (const auto* raw = get_payload_if<RawPayload>(payload)) {
    std::printf("  raw %zu bytes\n", raw->data.size());
  }
}

/** @brief Build and encode a console message frame. */
auto console_frame(const std::string& message) -> std::vector<std::byte> {
  game::OnConsoleMessage packet;
  packet.message = message;
  return PacketDecoder::encode(packet);
}

/** @brief Build a SendItemDatabaseData frame with a compressed fake items.dat. */
auto items_frame() -> std::vector<std::byte> {
  std::vector<std::byte> raw{std::byte{27}, std::byte{0}, std::byte{2},
                             std::byte{0}, std::byte{0}, std::byte{0}};
  for (int i = 0; i < 64; ++i) {
    raw.push_back(static_cast<std::byte>(i));
  }

  uLongf size = compressBound(static_cast<uLong>(raw.size()));
  std::vector<std::byte> compressed(size);
  const int status =
      compress2(reinterpret_cast<Bytef*>(compressed.data()), &size,
                reinterpret_cast<const Bytef*>(raw.data()),
                static_cast<uLong>(raw.size()), Z_BEST_SPEED);
  if (status != Z_OK) {
    return {};
  }
  compressed.resize(size);

  GamePayload payload{};
  payload.packet.type = static_cast<uint8_t>(PacketType::SendItemDatabaseData);
  payload.packet.net_id = 0xFFFFFFFFu;
  payload.extra = compressed;
  return serialize_payload(payload);
}

/** @brief Send one frame to every connected peer. */
void broadcast(spx::net::Host& host, const std::vector<std::byte>& frame) {
  std::lock_guard<std::mutex> lock(g_peers_mutex);
  for (ENetPeer* peer : g_peers) {
    host.send(peer, frame, true);
  }
}

/** @brief Console thread, commands are plain stdin lines. */
void read_console(spx::net::Host& host) {
  std::string line;
  while (g_running && std::getline(std::cin, line)) {
    if (line == "quit") {
      g_running = false;
      break;
    }
    if (line.starts_with("broadcast ")) {
      broadcast(host, console_frame(line.substr(10)));
    }
    std::fflush(stdout);
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

  auto host = spx::net::Host::create(kListenPort, 64, spx::net::HostRole::Server);
  if (!host) {
    std::printf("bind failed on port %u\n", kListenPort);
    spx::net::deinitialize();
    return 1;
  }

  PacketRegistry::instance().register_packet<game::OnConsoleMessage>();
  std::printf("core %s listening on 127.0.0.1:%u\n", spx::version().data(),
              kListenPort);
  std::printf("commands: broadcast <text> | quit\n");

  std::thread console{read_console, std::ref(*host)};

  while (g_running) {
    auto event = host->service(50);
    if (!event) {
      continue;
    }
    switch (event->type) {
      case spx::net::EventType::Connect: {
        std::lock_guard<std::mutex> lock(g_peers_mutex);
        g_peers.push_back(event->peer);
        std::printf("[connect] peer %u\n", event->peer->connectID);
        host->send(event->peer, console_frame("welcome to core test"), true);
        // Simulate the Growtopia redirect flow so the proxy shows its
        // OnSendToServer interception.
        game::OnSendToServer redirect;
        redirect.port = kListenPort;
        redirect.token = 1234;
        redirect.user = 7;
        redirect.address = "127.0.0.1";
        redirect.door_id = "door";
        redirect.uuid_token = "uuid";
        redirect.login_mode = 1;
        redirect.username = "player";
        host->send(event->peer, PacketDecoder::encode(redirect), true);
        game::OnSuperMainStartAcceptLogonHrdxs47254722215a super;
        super.item_hash = 0x12345678u;
        super.cdn_host = "127.0.0.1";
        super.cache_path = "cache";
        host->send(event->peer, serialize_payload(super.write()), true);
        break;
      }
      case spx::net::EventType::Disconnect: {
        std::printf("[disconnect] peer %u\n", event->peer->connectID);
        std::lock_guard<std::mutex> lock(g_peers_mutex);
        std::erase(g_peers, event->peer);
        break;
      }
      case spx::net::EventType::Receive: {
        auto payload = PacketDecoder::parse_payload(event->data);
        auto decoded = PacketDecoder::decode(event->data);
        if (!payload || !decoded) {
          std::printf("[recv] malformed %zu bytes\n", event->data.size());
          break;
        }
        std::printf("[recv] %s\n", packet_name((*decoded)->id()).data());
        print_payload(*payload);
        const auto* text = get_payload_if<TextPayload>(*payload);
        if (text != nullptr && text->data == "action|refresh_item_data") {
          const auto items = items_frame();
          if (!items.empty()) {
            host->send(event->peer, items, true);
            std::printf("[send] items.dat %zu bytes\n", items.size());
          }
        } else {
          host->send(event->peer, console_frame("server got your packet"), true);
        }
        break;
      }
      case spx::net::EventType::None:
        break;
    }
  }

  console.join();
  std::printf("bye\n");
  spx::net::deinitialize();
  return 0;
}
