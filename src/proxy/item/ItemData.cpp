#include "ItemStore.hpp"

#include <array>
#include <cstdint>
#include <mutex>
#include <utility>
#include <vector>

extern "C" {
#include <zlib.h>
}

#include "specialproxy/item/ItemDatabase.hpp"
#include "specialproxy/utils/Logger.hpp"

namespace spx::item {

namespace {

constexpr size_t kMaxItemsDatBytes = 64u * 1024u * 1024u;

/** @brief Inflate one zlib stream with a hard output bound. */
auto inflate_items(std::span<const std::byte> compressed,
                   std::vector<std::byte> &output) -> bool {
  if (compressed.empty()) {
    return false;
  }

  z_stream stream{};
  stream.next_in =
      reinterpret_cast<Bytef *>(const_cast<std::byte *>(compressed.data()));
  stream.avail_in = static_cast<uInt>(compressed.size());
  if (inflateInit(&stream) != Z_OK) {
    return false;
  }

  std::array<std::byte, 8192> buffer{};
  bool ok = false;
  for (;;) {
    stream.next_out = reinterpret_cast<Bytef *>(buffer.data());
    stream.avail_out = static_cast<uInt>(buffer.size());
    const int status = inflate(&stream, Z_NO_FLUSH);
    if (status != Z_OK && status != Z_STREAM_END) {
      break;
    }
    const size_t produced = buffer.size() - stream.avail_out;
    if (output.size() + produced > kMaxItemsDatBytes) {
      break;
    }
    output.insert(output.end(), buffer.begin(), buffer.begin() + produced);
    if (status == Z_STREAM_END) {
      ok = true;
      break;
    }
    if (stream.avail_in == 0 && stream.avail_out != 0) {
      break;
    }
  }

  inflateEnd(&stream);
  return ok && !output.empty();
}

} // namespace

auto ItemStore::instance() -> ItemStore & {
  static ItemStore store;
  return store;
}

auto ItemStore::accept(std::span<const std::byte> compressed) -> bool {
  std::vector<std::byte> bytes;
  if (!inflate_items(compressed, bytes)) {
    spx::log::warningf("item", "upstream items.dat decompression failed");
    return false;
  }
  if (bytes.size() < 6) {
    spx::log::warningf("item", "upstream items.dat header truncated");
    return false;
  }

  ItemData next;
  next.version = std::to_integer<uint8_t>(bytes[0]);
  next.count = static_cast<uint32_t>(std::to_integer<uint8_t>(bytes[2])) |
               (static_cast<uint32_t>(std::to_integer<uint8_t>(bytes[3])) << 8);
  next.checksum = static_cast<uint32_t>(
      crc32(0, reinterpret_cast<const Bytef *>(bytes.data()),
            static_cast<uInt>(bytes.size())));
  next.bytes = std::move(bytes);
  if (!ItemDatabase::instance().parse(next.bytes)) {
    spx::log::warningf("item", "items.dat database parse failed");
  }
  next.valid = true;

  std::lock_guard<std::mutex> lock{mutex_};
  if (current_.valid && current_.checksum == next.checksum) {
    return true;
  }
  current_ = std::move(next);
  request_pending_ = false;
  spx::log::infof("item",
                  "upstream items.dat version=%u count=%u bytes=%zu crc=%08x",
                  current_.version, current_.count, current_.bytes.size(),
                  current_.checksum);
  return true;
}

auto ItemStore::note_upstream_hash(uint32_t hash) -> bool {
  if (hash == 0) {
    return false;
  }
  std::lock_guard<std::mutex> lock{mutex_};
  if (current_.valid) {
    return false;
  }
  if (request_pending_ && expected_hash_ == hash) {
    return false;
  }
  expected_hash_ = hash;
  request_pending_ = true;
  spx::log::infof("item", "upstream item hash %08x, requesting items.dat",
                  hash);
  return true;
}

auto ItemStore::snapshot() const -> ItemData {
  std::lock_guard<std::mutex> lock{mutex_};
  return current_;
}

auto ItemStore::ready() const noexcept -> bool {
  std::lock_guard<std::mutex> lock{mutex_};
  return current_.valid;
}

} // namespace spx::item

namespace spx {

auto item_data() -> ItemData { return item::ItemStore::instance().snapshot(); }

auto item_data_ready() noexcept -> bool {
  return item::ItemStore::instance().ready();
}

} // namespace spx
