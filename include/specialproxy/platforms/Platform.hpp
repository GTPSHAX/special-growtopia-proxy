/** @file
 *  @brief Platform specific host file and CA certificate management.
 */

#ifndef SPXCORE_PLATFORMS_PLATFORM_HPP
#define SPXCORE_PLATFORMS_PLATFORM_HPP

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace spx::platform {

/** Descriptive information about the machine running the core. */
struct DeviceInfo {
  /** Host name reported by the operating system. */
  std::string hostname;
  /** Human readable operating system name and version. */
  std::string os;
  /** Platform identifier, one of windows, linux, macos or android. */
  std::string platform;
  /** CPU architecture, one of x86, x64, arm or arm64. */
  std::string arch;
  /** Number of logical processors available to the process. */
  uint32_t cpu_count{0};
};

/**
 * @brief Read descriptive information about the current machine.
 *
 * The result contains no credentials, identifiers or network addresses.
 *
 * @return Host name, operating system, platform, architecture and CPU count.
 */
auto device_info() -> DeviceInfo;

/** One hostname to IP mapping managed by the proxy. */
struct HostEntry {
  std::string hostname; ///< Name resolved on the device.
  std::string ip;       ///< Address the name points to.
};

/**
 * @brief Path of the hosts file on the running platform.
 *
 * @return Absolute path of the platform hosts file.
 */
auto hosts_path() -> std::string;

/**
 * @brief Host entries managed by the proxy inside its hosts block.
 *
 * @return Entries currently written between the proxy markers.
 */
auto list_host_entries() -> std::vector<HostEntry>;

/**
 * @brief Add or update a hostname mapping in the hosts file.
 *
 * Entries are written into a marked block so removal stays clean. Adding
 * an existing hostname updates its address, adding an identical entry
 * keeps the file unchanged.
 *
 * @param hostname Name to resolve.
 * @param ip Address the name points to.
 * @return True when the hosts file holds the mapping.
 */
auto add_host_entry(std::string_view hostname, std::string_view ip) -> bool;

/**
 * @brief Add or update several host mappings in one pass.
 *
 * Active mappings outside the managed block that resolve the same names
 * are disabled and stored inside the block so removal restores them.
 *
 * @param entries Mappings to install.
 * @return True when every mapping is present in the hosts file.
 */
auto add_host_entries(std::span<const HostEntry> entries) -> bool;

/**
 * @brief Remove every host entry managed by the proxy.
 *
 * Original lines disabled by an earlier install are restored verbatim.
 *
 * @return True when the hosts file no longer holds proxy entries.
 */
auto remove_host_entries() -> bool;

/**
 * @brief Install a CA certificate into the platform trust store.
 *
 * @param der DER encoded certificate body.
 * @return True when the store accepted the certificate.
 */
auto install_ca_certificate(std::span<const std::byte> der) -> bool;

/**
 * @brief Remove a CA certificate previously installed by the proxy.
 *
 * @param der DER encoded certificate body to remove.
 * @return True when the certificate is gone from the trust store.
 */
auto uninstall_ca_certificate(std::span<const std::byte> der) -> bool;

/**
 * @brief Remove proxy CAs left behind by earlier runs.
 *
 * Only self signed CAs with the proxy common name are removed. The
 * certificate still in use is kept when its DER body is supplied.
 *
 * @param common_name Subject common name owned by the proxy.
 * @param keep_der DER body of the certificate to keep, or empty.
 * @return True when no stale proxy CA remains.
 */
auto remove_stale_ca_certificates(std::string_view common_name,
                                  std::span<const std::byte> keep_der) -> bool;

/**
 * @brief Message of the last failed platform operation.
 *
 * @return Error text of the failing call on this thread, or an empty view.
 */
auto last_error() -> std::string_view;

} // namespace spx::platform

#endif // SPXCORE_PLATFORMS_PLATFORM_HPP
