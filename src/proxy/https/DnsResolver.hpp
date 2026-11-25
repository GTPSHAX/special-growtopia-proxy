/** @file
 *  @brief Minimal DNS A record resolver that queries public servers directly.
 */

#ifndef SPXCORE_HTTPS_DNS_RESOLVER_HPP
#define SPXCORE_HTTPS_DNS_RESOLVER_HPP

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace spx::https::dns {

/**
 * @brief Resolve a hostname to an IPv4 dotted address over UDP DNS.
 *
 * The system resolver is bypassed on purpose, the hosts file normally points
 * the proxied name at the loopback address. Servers are tried in order until
 * one returns an A record.
 *
 * @param hostname Name to look up.
 * @param servers IP addresses of recursive DNS servers.
 * @param timeout_ms Socket timeout for each server, in milliseconds.
 * @return Dotted IPv4 address, or nullopt when no server answered.
 */
auto resolve(std::string_view hostname, std::span<const std::string> servers,
             int timeout_ms) -> std::optional<std::string>;

namespace detail {

/**
 * @brief Build a standard query with the recursion desired flag set.
 *
 * @param hostname Name to encode into the question section.
 * @param id Transaction identifier written into the header.
 * @return Wire format query, or nullopt for an invalid name.
 */
auto build_query(std::string_view hostname, uint16_t id)
    -> std::optional<std::vector<uint8_t>>;

/**
 * @brief Extract the first A record from a DNS response.
 *
 * @param packet Raw UDP payload.
 * @param id Expected transaction identifier.
 * @return Dotted IPv4 address, or nullopt on mismatch or error.
 */
auto parse_response(std::span<const uint8_t> packet, uint16_t id)
    -> std::optional<std::string>;

} // namespace detail

} // namespace spx::https::dns

#endif // SPXCORE_HTTPS_DNS_RESOLVER_HPP
