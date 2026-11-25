/** @file
 *  @brief OpenSSL based generation of the CA and leaf certificate pair.
 */

#include "CertificateFactory.hpp"

#include <memory>

#include <openssl/bio.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>

namespace spx::https::detail {

namespace {

struct X509Guard {
  void operator()(X509 *value) const { X509_free(value); }
};

struct PkeyGuard {
  void operator()(EVP_PKEY *value) const { EVP_PKEY_free(value); }
};

struct BioGuard {
  void operator()(BIO *value) const { BIO_free(value); }
};

using X509Ptr = std::unique_ptr<X509, X509Guard>;
using PkeyPtr = std::unique_ptr<EVP_PKEY, PkeyGuard>;
using BioPtr = std::unique_ptr<BIO, BioGuard>;

/** Validity window of the generated certificates in seconds. */
constexpr long kValiditySeconds{3650L * 24 * 60 * 60};

/** @brief Random positive serial number for a new certificate. */
auto random_serial() -> ASN1_INTEGER * {
  uint64_t raw = 0;
  if (RAND_bytes(reinterpret_cast<unsigned char *>(&raw), sizeof raw) != 1) {
    return nullptr;
  }
  raw >>= 1;
  ASN1_INTEGER *serial = ASN1_INTEGER_new();
  if (serial == nullptr || ASN1_INTEGER_set_uint64(serial, raw) != 1) {
    ASN1_INTEGER_free(serial);
    return nullptr;
  }
  return serial;
}

/** @brief Attach one X509 v3 extension text to a certificate. */
auto add_extension(X509 *subject, X509 *issuer, int nid, const char *value)
    -> bool {
  X509V3_CTX context;
  X509V3_set_ctx(&context, issuer, subject, nullptr, nullptr, 0);
  X509_EXTENSION *extension =
      X509V3_EXT_conf_nid(nullptr, &context, nid, value);
  if (extension == nullptr) {
    return false;
  }
  const int added = X509_add_ext(subject, extension, -1);
  X509_EXTENSION_free(extension);
  return added == 1;
}

/** @brief Render one certificate or key through a PEM writer callback. */
template <typename Writer>
auto pem_string(const Writer &writer) -> std::string {
  BioPtr bio{BIO_new(BIO_s_mem())};
  if (bio == nullptr || writer(bio.get()) != 1) {
    return {};
  }
  char *data = nullptr;
  const long size = BIO_get_mem_data(bio.get(), &data);
  if (size <= 0) {
    return {};
  }
  return std::string{data, static_cast<size_t>(size)};
}

/** @brief DER encoding of one certificate for the trust store. */
auto der_bytes(X509 *certificate) -> std::vector<std::byte> {
  unsigned char *raw = nullptr;
  const int size = i2d_X509(certificate, &raw);
  if (size <= 0) {
    return {};
  }
  std::vector<std::byte> bytes{reinterpret_cast<std::byte *>(raw),
                               reinterpret_cast<std::byte *>(raw) +
                                   static_cast<size_t>(size)};
  OPENSSL_free(raw);
  return bytes;
}

/** @brief Apply identity, validity and key material to one certificate. */
auto stamp_certificate(X509 *certificate, EVP_PKEY *key, X509 *issuer,
                       const char *common_name) -> bool {
  X509_set_version(certificate, 2);
  ASN1_INTEGER *serial = random_serial();
  if (serial == nullptr) {
    return false;
  }
  X509_set_serialNumber(certificate, serial);
  ASN1_INTEGER_free(serial);
  X509_gmtime_adj(X509_getm_notBefore(certificate), -3600);
  X509_gmtime_adj(X509_getm_notAfter(certificate), kValiditySeconds);
  X509_NAME *name = X509_get_subject_name(certificate);
  if (X509_NAME_add_entry_by_txt(
          name, "CN", MBSTRING_ASC,
          reinterpret_cast<const unsigned char *>(common_name), -1, -1,
          0) != 1) {
    return false;
  }
  X509_set_issuer_name(certificate, X509_get_subject_name(issuer));
  return X509_set_pubkey(certificate, key) == 1;
}

} // namespace

auto generate_certificate_pair(std::span<const std::string> hostnames)
    -> std::optional<CertificatePair> {
  if (hostnames.empty()) {
    return std::nullopt;
  }
  for (const auto &hostname : hostnames) {
    if (hostname.empty() || hostname.size() > 255) {
      return std::nullopt;
    }
  }
  PkeyPtr ca_key{EVP_RSA_gen(2048)};
  PkeyPtr leaf_key{EVP_RSA_gen(2048)};
  X509Ptr ca{X509_new()};
  X509Ptr leaf{X509_new()};
  if (!ca_key || !leaf_key || !ca || !leaf) {
    return std::nullopt;
  }

  if (!stamp_certificate(ca.get(), ca_key.get(), ca.get(), "specialproxy CA") ||
      !add_extension(ca.get(), ca.get(), NID_basic_constraints,
                     "critical,CA:TRUE") ||
      !add_extension(ca.get(), ca.get(), NID_key_usage,
                     "critical,keyCertSign,cRLSign") ||
      !add_extension(ca.get(), ca.get(), NID_subject_key_identifier, "hash")) {
    return std::nullopt;
  }
  if (X509_sign(ca.get(), ca_key.get(), EVP_sha256()) == 0) {
    return std::nullopt;
  }

  std::string alt_names;
  for (const auto &hostname : hostnames) {
    if (!alt_names.empty()) {
      alt_names.push_back(',');
    }
    alt_names += "DNS:";
    alt_names += hostname;
  }
  if (!stamp_certificate(leaf.get(), leaf_key.get(), ca.get(),
                         hostnames.front().c_str()) ||
      !add_extension(leaf.get(), ca.get(), NID_basic_constraints,
                     "critical,CA:FALSE") ||
      !add_extension(leaf.get(), ca.get(), NID_subject_alt_name,
                     alt_names.c_str()) ||
      !add_extension(leaf.get(), ca.get(), NID_key_usage,
                     "critical,digitalSignature,keyEncipherment") ||
      !add_extension(leaf.get(), ca.get(), NID_ext_key_usage, "serverAuth") ||
      !add_extension(leaf.get(), ca.get(), NID_authority_key_identifier,
                     "keyid:always")) {
    return std::nullopt;
  }
  if (X509_sign(leaf.get(), ca_key.get(), EVP_sha256()) == 0) {
    return std::nullopt;
  }

  CertificatePair pair;
  pair.ca_pem =
      pem_string([&](BIO *bio) { return PEM_write_bio_X509(bio, ca.get()); });
  pair.cert_pem =
      pem_string([&](BIO *bio) { return PEM_write_bio_X509(bio, leaf.get()); });
  pair.key_pem = pem_string([&](BIO *bio) {
    return PEM_write_bio_PrivateKey(bio, leaf_key.get(), nullptr, nullptr, 0,
                                    nullptr, nullptr);
  });
  pair.ca_der = der_bytes(ca.get());
  if (pair.ca_pem.empty() || pair.cert_pem.empty() || pair.key_pem.empty() ||
      pair.ca_der.empty()) {
    return std::nullopt;
  }
  return pair;
}

} // namespace spx::https::detail
