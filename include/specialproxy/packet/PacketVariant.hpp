/** @file
 *  @brief Variant list values and their wire encoding for function calls.
 */

#ifndef SPXCORE_PACKET_PACKET_VARIANT_HPP
#define SPXCORE_PACKET_PACKET_VARIANT_HPP

#include <array>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace spx::packet {

/** 2D float vector carried by variant parameters. */
struct Vec2 {
  float x{0.0F}; ///< Horizontal component.
  float y{0.0F}; ///< Vertical component.
};

/** 3D float vector carried by variant parameters. */
struct Vec3 {
  float x{0.0F}; ///< X component.
  float y{0.0F}; ///< Y component.
  float z{0.0F}; ///< Z component.
};

/** Maximum number of variant parameters in one function call. */
inline constexpr size_t kMaxVariants = 7;

/** Wire type codes used by the client variant dispatcher. */
enum class VariantType : uint8_t {
  Float = 1,
  String = 2,
  Vec2 = 3,
  Vec3 = 4,
  UInt32 = 5,
  Int32 = 9,
};

/** Ordered value types matching the VariantType wire codes. */
using VariantValue = std::variant<float, std::string, Vec2, Vec3, uint32_t, int32_t>;

/** @brief Wire code for a stored variant value. */
[[nodiscard]] inline auto wire_type(const VariantValue& value) -> VariantType {
  switch (value.index()) {
    case 0: return VariantType::Float;
    case 1: return VariantType::String;
    case 2: return VariantType::Vec2;
    case 3: return VariantType::Vec3;
    case 4: return VariantType::UInt32;
    default: return VariantType::Int32;
  }
}

/** Fixed slot list for variant parameters, addressed by wire index. */
class VariantList {
 public:
  /** @brief Store a value at a wire index. */
  auto set(size_t index, VariantValue value) -> bool {
    if (index >= kMaxVariants) {
      return false;
    }
    if (!slots_[index]) {
      count_ += 1;
    }
    slots_[index] = std::move(value);
    return true;
  }

  /** @brief Read-only access to a slot, null when unset. */
  [[nodiscard]] auto get(size_t index) const -> const VariantValue* {
    if (index >= kMaxVariants) {
      return nullptr;
    }
    return slots_[index] ? &*slots_[index] : nullptr;
  }

  /** @brief Typed access to a slot, null when unset or of another type. */
  template <typename T>
  [[nodiscard]] auto get(size_t index) const -> const T* {
    const VariantValue* value = get(index);
    return value ? std::get_if<T>(value) : nullptr;
  }

  /** @brief Number of set slots. */
  [[nodiscard]] constexpr auto size() const noexcept -> size_t {
    return count_;
  }

  /** @brief Serialize into the wire form of a variant list. */
  [[nodiscard]] auto serialize() const -> std::vector<std::byte> {
    std::vector<std::byte> out;
    out.reserve(1 + count_ * 9);
    out.push_back(std::byte{static_cast<uint8_t>(count_)});
    for (size_t i = 0; i < kMaxVariants; ++i) {
      if (!slots_[i]) {
        continue;
      }
      out.push_back(std::byte{static_cast<uint8_t>(i)});
      out.push_back(std::byte{static_cast<uint8_t>(wire_type(*slots_[i]))});
      append_value(out, *slots_[i]);
    }
    return out;
  }

  /** @brief Parse a variant list from its wire form.
   *
   * @param data Wire bytes holding the count byte followed by elements.
   * @return Parsed list, or nullopt when the encoding is malformed.
   */
  [[nodiscard]] static auto parse(std::span<const std::byte> data)
      -> std::optional<VariantList> {
    if (data.empty()) {
      return std::nullopt;
    }
    const size_t count = std::to_integer<size_t>(data[0]);
    if (count > kMaxVariants) {
      return std::nullopt;
    }
    VariantList out;
    size_t cursor = 1;
    for (size_t i = 0; i < count; ++i) {
      if (cursor + 2 > data.size()) {
        return std::nullopt;
      }
      const size_t index = std::to_integer<size_t>(data[cursor]);
      const auto type = static_cast<VariantType>(std::to_integer<uint8_t>(data[cursor + 1]));
      cursor += 2;
      if (index >= kMaxVariants) {
        return std::nullopt;
      }
      VariantValue value;
      if (!read_value(data, cursor, type, value)) {
        return std::nullopt;
      }
      out.slots_[index] = std::move(value);
    }
    out.recount();
    return out;
  }

 private:
  constexpr void recount() {
    count_ = 0;
    for (const auto& slot : slots_) {
      if (slot) {
        count_ += 1;
      }
    }
  }

  static void append_value(std::vector<std::byte>& out, const VariantValue& value) {
    switch (wire_type(value)) {
      case VariantType::Float: {
        append_trivial(out, std::get<float>(value));
        break;
      }
      case VariantType::String: {
        const auto& text = std::get<std::string>(value);
        const uint32_t length = static_cast<uint32_t>(text.size());
        append_trivial(out, length);
        const auto* bytes = reinterpret_cast<const std::byte*>(text.data());
        out.insert(out.end(), bytes, bytes + text.size());
        break;
      }
      case VariantType::Vec2: {
        const auto& v = std::get<Vec2>(value);
        append_trivial(out, v.x);
        append_trivial(out, v.y);
        break;
      }
      case VariantType::Vec3: {
        const auto& v = std::get<Vec3>(value);
        append_trivial(out, v.x);
        append_trivial(out, v.y);
        append_trivial(out, v.z);
        break;
      }
      case VariantType::UInt32: {
        append_trivial(out, std::get<uint32_t>(value));
        break;
      }
      case VariantType::Int32: {
        append_trivial(out, std::get<int32_t>(value));
        break;
      }
    }
  }

  template <typename T>
  static void append_trivial(std::vector<std::byte>& out, const T& value) {
    static_assert(std::is_trivially_copyable_v<T>);
    const auto* bytes = reinterpret_cast<const std::byte*>(&value);
    out.insert(out.end(), bytes, bytes + sizeof(T));
  }

  static bool read_value(std::span<const std::byte> data, size_t& cursor,
                         VariantType type, VariantValue& value) {
    const size_t remaining = data.size() - cursor;
    switch (type) {
      case VariantType::Float: {
        if (remaining < 4) {
          return false;
        }
        float f = 0.0F;
        std::memcpy(&f, data.data() + cursor, 4);
        cursor += 4;
        value = f;
        return true;
      }
      case VariantType::String: {
        if (remaining < 4) {
          return false;
        }
        uint32_t length = 0;
        std::memcpy(&length, data.data() + cursor, 4);
        cursor += 4;
        if (length > data.size() - cursor) {
          return false;
        }
        const auto* chars = reinterpret_cast<const char*>(data.data() + cursor);
        value = std::string{chars, length};
        cursor += length;
        return true;
      }
      case VariantType::Vec2: {
        if (remaining < 8) {
          return false;
        }
        Vec2 v;
        std::memcpy(&v.x, data.data() + cursor, 4);
        std::memcpy(&v.y, data.data() + cursor + 4, 4);
        cursor += 8;
        value = v;
        return true;
      }
      case VariantType::Vec3: {
        if (remaining < 12) {
          return false;
        }
        Vec3 v;
        std::memcpy(&v.x, data.data() + cursor, 4);
        std::memcpy(&v.y, data.data() + cursor + 4, 4);
        std::memcpy(&v.z, data.data() + cursor + 8, 4);
        cursor += 12;
        value = v;
        return true;
      }
      case VariantType::UInt32: {
        if (remaining < 4) {
          return false;
        }
        uint32_t u = 0;
        std::memcpy(&u, data.data() + cursor, 4);
        cursor += 4;
        value = u;
        return true;
      }
      case VariantType::Int32: {
        if (remaining < 4) {
          return false;
        }
        int32_t s = 0;
        std::memcpy(&s, data.data() + cursor, 4);
        cursor += 4;
        value = s;
        return true;
      }
      default:
        return false;
    }
  }

  std::array<std::optional<VariantValue>, kMaxVariants> slots_{};
  size_t count_{0};
};

} // namespace spx::packet

#endif // SPXCORE_PACKET_PACKET_VARIANT_HPP
