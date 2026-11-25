/** @file
 *  @brief Public entry points for loading Lua feature scripts.
 */

#ifndef SPXCORE_SCRIPT_SCRIPT_HPP
#define SPXCORE_SCRIPT_SCRIPT_HPP

#include <string_view>

namespace spx {

/**
 * @brief Queue a Lua source string to be loaded on the next proxy start.
 *
 * Scripts execute in the order they are queued. Queued sources are consumed
 * by proxy_start and remain valid for the lifetime of that proxy run.
 *
 * @param[in] source Lua source code.
 * @param[in] name Label used in diagnostics, empty for an anonymous script.
 * @return True when the source was queued.
 */
auto script_load(std::string_view source, std::string_view name = {}) -> bool;

/**
 * @brief Queue a Lua file to be loaded on the next proxy start.
 *
 * @param[in] path Path to a UTF-8 Lua source file.
 * @return True when the file was read and queued.
 */
auto script_load_file(std::string_view path) -> bool;

/**
 * @brief Set the folder scanned for Lua files on proxy start.
 *
 * Files with a .lua extension are loaded in lexical order. The default
 * folder is "scripts" relative to the current working directory.
 *
 * @param[in] path Folder path.
 */
void script_set_directory(std::string_view path);

/**
 * @brief Remove all queued scripts and restore the default script folder.
 */
void script_clear();

} // namespace spx

#endif // SPXCORE_SCRIPT_SCRIPT_HPP
