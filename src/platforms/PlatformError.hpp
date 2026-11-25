/** @file
 *  @brief Shared per thread error slot for the platform implementations.
 */

#ifndef SPXCORE_PLATFORMS_PLATFORM_ERROR_HPP
#define SPXCORE_PLATFORMS_PLATFORM_ERROR_HPP

#include <string>
#include <string_view>

namespace spx::platform::detail {

/** @brief Store the error text of the failing call on this thread. */
void set_last_error(std::string message);

/** @brief Redirect hosts operations to a test file, empty restores the platform
 * path. */
void set_hosts_path_override(std::string path);

} // namespace spx::platform::detail

#endif // SPXCORE_PLATFORMS_PLATFORM_ERROR_HPP
