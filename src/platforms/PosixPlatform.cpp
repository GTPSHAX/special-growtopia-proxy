/** @file
 *  @brief Linux, Android and macOS implementation of the platform API.
 */

#ifndef _WIN32

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <sys/utsname.h>
#include <unistd.h>
#if defined(__ANDROID__)
#include <sys/system_properties.h>
#endif

#include "PlatformError.hpp"
#include "specialproxy/platforms/Platform.hpp"

namespace spx::platform {

namespace {

/** @brief Base64 encode DER bytes for the PEM body. */
auto base64(std::span<const std::byte> data) -> std::string {
  static constexpr char kAlphabet[] =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  std::string out;
  out.reserve((data.size() + 2) / 3 * 4);
  for (size_t i = 0; i < data.size(); i += 3) {
    const unsigned group =
        static_cast<unsigned>(std::to_integer<unsigned>(data[i])) << 16 |
        (i + 1 < data.size() ? std::to_integer<unsigned>(data[i + 1]) << 8
                             : 0u) |
        (i + 2 < data.size() ? std::to_integer<unsigned>(data[i + 2]) : 0u);
    out.push_back(kAlphabet[(group >> 18) & 0x3F]);
    out.push_back(kAlphabet[(group >> 12) & 0x3F]);
    out.push_back(i + 1 < data.size() ? kAlphabet[(group >> 6) & 0x3F] : '=');
    out.push_back(i + 2 < data.size() ? kAlphabet[group & 0x3F] : '=');
  }
  return out;
}

/** @brief Wrap DER bytes into a PEM certificate document. */
auto to_pem(std::span<const std::byte> der) -> std::string {
  const std::string encoded = base64(der);
  std::string pem{"-----BEGIN CERTIFICATE-----\n"};
  for (size_t i = 0; i < encoded.size(); i += 64) {
    pem += encoded.substr(i, 64);
    pem.push_back('\n');
  }
  pem += "-----END CERTIFICATE-----\n";
  return pem;
}

/** @brief Write a whole file, replacing its content. */
auto write_file(std::string_view path, std::string_view content) -> bool {
  FILE *file = std::fopen(std::string{path}.c_str(), "wb");
  if (file == nullptr) {
    detail::set_last_error("cannot write " + std::string{path});
    return false;
  }
  const size_t written = std::fwrite(content.data(), 1, content.size(), file);
  std::fclose(file);
  if (written != content.size()) {
    detail::set_last_error("write failed on " + std::string{path});
    return false;
  }
  return true;
}

} // namespace

auto hosts_path() -> std::string {
#if defined(__ANDROID__)
  return "/system/etc/hosts";
#else
  return "/etc/hosts";
#endif
}

auto install_ca_certificate(std::span<const std::byte> der) -> bool {
  if (der.empty()) {
    detail::set_last_error("empty certificate");
    return false;
  }
  const std::string pem = to_pem(der);
#if defined(__ANDROID__)
  // Programmatic install needs a rooted remount of the system CA store.
  if (!write_file("/system/etc/security/cacerts/specialproxy.crt", pem)) {
    detail::set_last_error("Android CA install requires root, open the device "
                           "settings instead");
    return false;
  }
  return true;
#elif defined(__APPLE__)
  if (!write_file("/tmp/specialproxy-ca.crt", pem)) {
    return false;
  }
  const int result = std::system(
      "security add-trusted-cert -d -r trustRoot "
      "-k /Library/Keychains/System.keychain /tmp/specialproxy-ca.crt");
  if (result != 0) {
    detail::set_last_error(
        "security add-trusted-cert failed, run as administrator");
    return false;
  }
  return true;
#else
  if (!write_file("/usr/local/share/ca-certificates/specialproxy.crt", pem)) {
    return false;
  }
  const int result = std::system("update-ca-certificates");
  if (result != 0) {
    detail::set_last_error(
        "update-ca-certificates failed, run as administrator");
    return false;
  }
  return true;
#endif
}

auto uninstall_ca_certificate(std::span<const std::byte> der) -> bool {
  if (der.empty()) {
    detail::set_last_error("empty certificate");
    return false;
  }
#if defined(__ANDROID__)
  (void)der;
  detail::set_last_error("Android CA install is not performed by the core");
  return false;
#elif defined(__APPLE__)
  (void)der;
  const int result =
      std::system("security delete-certificate -c \"specialproxy CA\" "
                  "/Library/Keychains/System.keychain");
  if (result != 0) {
    detail::set_last_error(
        "specialproxy CA could not be removed from the keychain");
    return false;
  }
  return true;
#else
  (void)der;
  if (std::remove("/usr/local/share/ca-certificates/specialproxy.crt") != 0 &&
      errno != ENOENT) {
    detail::set_last_error("specialproxy CA file could not be removed");
    return false;
  }
  const int result = std::system("update-ca-certificates");
  if (result != 0) {
    detail::set_last_error(
        "update-ca-certificates failed, run as administrator");
    return false;
  }
  return true;
#endif
}

auto remove_stale_ca_certificates(std::string_view common_name,
                                  std::span<const std::byte> keep_der) -> bool {
  (void)common_name;
#if defined(__APPLE__)
  // A fixed file or keychain label prevents duplicates on the other systems.
  // Deleting by common name would also drop the certificate still in use.
  if (!keep_der.empty()) {
    return true;
  }
  std::system("security delete-certificate -c \"specialproxy CA\" "
              "/Library/Keychains/System.keychain > /dev/null 2>&1");
#else
  (void)keep_der;
#endif
  return true;
}

auto device_info() -> DeviceInfo {
  DeviceInfo info;
  char host[256]{};
  if (::gethostname(host, sizeof host) == 0) {
    host[sizeof host - 1] = '\0';
    info.hostname = host;
  }
  utsname kernel{};
  const bool have_kernel = ::uname(&kernel) == 0;
#if defined(__ANDROID__)
  info.platform = "android";
  char release[128]{};
  if (__system_property_get("ro.build.version.release", release) > 0) {
    info.os = "Android " + std::string{release};
  } else if (have_kernel) {
    info.os = std::string{kernel.sysname} + " " + kernel.release;
  }
#elif defined(__APPLE__)
  info.platform = "macos";
  if (have_kernel) {
    info.os = "macOS " + std::string{kernel.release};
  }
#else
  info.platform = "linux";
  if (have_kernel) {
    info.os = std::string{kernel.sysname} + " " + kernel.release;
  }
#endif
#if defined(__x86_64__)
  info.arch = "x64";
#elif defined(__i386__)
  info.arch = "x86";
#elif defined(__aarch64__)
  info.arch = "arm64";
#elif defined(__arm__)
  info.arch = "arm";
#else
  info.arch = "unknown";
#endif
  const long cpus = ::sysconf(_SC_NPROCESSORS_ONLN);
  info.cpu_count = cpus > 0 ? static_cast<uint32_t>(cpus) : 0u;
  return info;
}

} // namespace spx::platform

#endif // _WIN32
