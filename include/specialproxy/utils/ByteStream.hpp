/** @file
 *  @brief Bounds-checked little-endian byte stream for wire encoding.
 */

#ifndef SPXCORE_UTILS_BYTE_STREAM_HPP
#define SPXCORE_UTILS_BYTE_STREAM_HPP

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <vector>

namespace spx::utils {

/** @brief Sequential reader and writer for little-endian wire data.
 *
 *  @tparam LenType Integer type used for length prefixes on strings.
 */
template <typename LenType = uint32_t>
class ByteStream {
 public:
  /** @brief Construct a writing stream backed by an internal buffer. */
  ByteStream() = default;

  /**
   * @brief Construct a reading stream over existing bytes.
   *
   * @param data Buffer to read from, kept unowned.
   */
  explicit ByteStream(std::span<const std::byte> data) : data_{data} {}

  /**
   * @brief Append a trivially copyable value.
   *
   * @tparam T Value type, copied byte-wise.
   * @param value Value to append.
   */
  template <typename T>
  void write(const T& value) {
    static_assert(std::is_trivially_copyable_v<T>);
    const auto* bytes = reinterpret_cast<const std::byte*>(&value);
    buffer_.insert(buffer_.end(), bytes, bytes + sizeof(T));
  }

  /**
   * @brief Append raw bytes.
   *
   * @param data Bytes to append.
   */
  void write_data(std::span<const std::byte> data) {
    buffer_.insert(buffer_.end(), data.begin(), data.end());
  }

  /**
   * @brief Append a string with a length prefix.
   *
   * @param text String to append.
   */
  void write(const std::string& text) {
    const LenType len = static_cast<LenType>(text.size());
    write(len);
    write_data({reinterpret_cast<const std::byte*>(text.data()), text.size()});
  }

  /** @brief Take ownership of the written buffer. */
  auto take_data() -> std::vector<std::byte> {
    return std::move(buffer_);
  }

  /**
   * @brief Read a trivially copyable value.
   *
   * @tparam T Value type, filled byte-wise.
   * @param[out] value Receives the read bytes.
   * @return False when fewer bytes than sizeof(T) remain.
   */
  template <typename T>
  auto read(T& value) -> bool {
    static_assert(std::is_trivially_copyable_v<T>);
    if (read_ + sizeof(T) > data_.size()) {
      return false;
    }
    std::memcpy(&value, data_.data() + read_, sizeof(T));
    read_ += sizeof(T);
    return true;
  }

  /**
   * @brief Read a length prefixed string.
   *
   * @param[out] text Receives the string body without the prefix.
   * @return False when the prefix or body runs past the buffer.
   */
  auto read(std::string& text) -> bool {
    LenType len = 0;
    if (!read(len)) {
      return false;
    }
    return read_data(text, len);
  }

  /**
   * @brief Read raw bytes into a string.
   *
   * @param[out] text Receives exactly length bytes.
   * @param length Number of bytes to read.
   * @return False when fewer bytes remain.
   */
  auto read_data(std::string& text, size_t length) -> bool {
    if (read_ + length > data_.size()) {
      return false;
    }
    text.assign(reinterpret_cast<const char*>(data_.data() + read_), length);
    read_ += length;
    return true;
  }

  /**
   * @brief Read raw bytes into a byte buffer.
   *
   * @param[out] out Receives exactly length bytes.
   * @param length Number of bytes to read.
   * @return False when fewer bytes remain.
   */
  auto read_data(std::byte* out, size_t length) -> bool {
    if (read_ + length > data_.size()) {
      return false;
    }
    std::memcpy(out, data_.data() + read_, length);
    read_ += length;
    return true;
  }

  /** @brief Bytes consumed so far. */
  auto read_offset() const noexcept -> size_t {
    return read_;
  }

  /** @brief Total bytes available for reading. */
  auto size() const noexcept -> size_t {
    return data_.size();
  }

 private:
  std::span<const std::byte> data_;
  size_t read_{0};
  std::vector<std::byte> buffer_;
};

} // namespace spx::utils

#endif // SPXCORE_UTILS_BYTE_STREAM_HPP
