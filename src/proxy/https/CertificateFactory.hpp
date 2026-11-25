/** @file
 *  @brief In memory generation of the CA and leaf certificate pair.
 */

#ifndef SPXCORE_HTTPS_CERTIFICATE_FACTORY_HPP
#define SPXCORE_HTTPS_CERTIFICATE_FACTORY_HPP

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace spx::https::detail {

/** PEM bodies plus the DER form of the CA. */
struct CertificatePair {
  std::string ca_pem;            ///< PEM of the self signed CA.
  std::string cert_pem;          ///< PEM of the leaf certificate.
  std::string key_pem;           ///< PEM private key of the leaf.
  std::vector<std::byte> ca_der; ///< DER of the CA for the trust store.
};

/**
 * @brief Create a self signed CA and a leaf certificate for the hostnames.
 *
 * The first name becomes the leaf subject CN. Every name is written into
 * the subject alternative name extension.
 *
 * @param hostnames DNS names served by the HTTPS listener.
 * @return The pair, or nullopt when OpenSSL failed.
 */
auto generate_certificate_pair(std::span<const std::string> hostnames)
    -> std::optional<CertificatePair>;

} // namespace spx::https::detail

#endif // SPXCORE_HTTPS_CERTIFICATE_FACTORY_HPP
