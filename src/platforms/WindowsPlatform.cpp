/** @file
 *  @brief Windows implementation of the platform API.
 */

#include <cctype>
#include <cstring>
#include <string>

#include <windows.h>
#include <wincrypt.h>

#include "PlatformError.hpp"
#include "specialproxy/platforms/Platform.hpp"
#include "specialproxy/platforms/detail/HostsBlock.hpp"

namespace spx::platform {

namespace {

/** @brief Open the writable LocalMachine ROOT store. */
auto open_root_store() -> HCERTSTORE {
  return CertOpenStore(CERT_STORE_PROV_SYSTEM_W, 0, 0,
                       CERT_SYSTEM_STORE_LOCAL_MACHINE, L"ROOT");
}

/** @brief Subject common name of a certificate in the store. */
auto common_name_of(PCCERT_CONTEXT certificate) -> std::string {
  char name[256]{};
  const DWORD size =
      CertGetNameStringA(certificate, CERT_NAME_SIMPLE_DISPLAY_TYPE, 0, nullptr,
                         name, sizeof name);
  if (size <= 1) {
    return {};
  }
  return std::string{name, static_cast<size_t>(size) - 1};
}

/** @brief Whether the store certificate carries the given DER body. */
auto same_der(PCCERT_CONTEXT certificate, std::span<const std::byte> der)
    -> bool {
  return !der.empty() && der.size() == certificate->cbCertEncoded &&
         std::memcmp(der.data(), certificate->pbCertEncoded, der.size()) == 0;
}

/** @brief Delete a certificate using a duplicate safe for enumeration. */
auto delete_certificate(HCERTSTORE store, PCCERT_CONTEXT certificate) -> bool {
  PCCERT_CONTEXT duplicate = CertDuplicateCertificateContext(certificate);
  if (duplicate == nullptr) {
    return false;
  }
  return CertDeleteCertificateFromStore(duplicate) == TRUE;
}

} // namespace

auto hosts_path() -> std::string {
  char directory[MAX_PATH]{};
  const UINT length = GetSystemDirectoryA(directory, MAX_PATH);
  if (length == 0) {
    return "C:\\Windows\\System32\\drivers\\etc\\hosts";
  }
  return std::string{directory} + "\\drivers\\etc\\hosts";
}

auto install_ca_certificate(std::span<const std::byte> der) -> bool {
  if (der.empty()) {
    detail::set_last_error("empty certificate");
    return false;
  }
  PCCERT_CONTEXT context =
      CertCreateCertificateContext(X509_ASN_ENCODING | PKCS_7_ASN_ENCODING,
                                   reinterpret_cast<const BYTE *>(der.data()),
                                   static_cast<DWORD>(der.size()));
  if (context == nullptr) {
    detail::set_last_error("certificate is not valid DER");
    return false;
  }
  HCERTSTORE store = open_root_store();
  if (store == nullptr) {
    CertFreeCertificateContext(context);
    detail::set_last_error(
        "cannot open the LocalMachine ROOT store, run as administrator");
    return false;
  }
  const BOOL added = CertAddCertificateContextToStore(
      store, context, CERT_STORE_ADD_REPLACE_EXISTING, nullptr);
  CertFreeCertificateContext(context);
  CertCloseStore(store, 0);
  if (!added) {
    detail::set_last_error("LocalMachine ROOT store rejected the certificate");
    return false;
  }
  return true;
}

auto uninstall_ca_certificate(std::span<const std::byte> der) -> bool {
  if (der.empty()) {
    detail::set_last_error("empty certificate");
    return false;
  }
  PCCERT_CONTEXT search =
      CertCreateCertificateContext(X509_ASN_ENCODING | PKCS_7_ASN_ENCODING,
                                   reinterpret_cast<const BYTE *>(der.data()),
                                   static_cast<DWORD>(der.size()));
  HCERTSTORE store = open_root_store();
  if (search == nullptr || store == nullptr) {
    if (search != nullptr) {
      CertFreeCertificateContext(search);
    }
    if (store != nullptr) {
      CertCloseStore(store, 0);
    }
    detail::set_last_error(
        "cannot open the LocalMachine ROOT store, run as administrator");
    return false;
  }
  PCCERT_CONTEXT found =
      CertFindCertificateInStore(store, X509_ASN_ENCODING | PKCS_7_ASN_ENCODING,
                                 0, CERT_FIND_EXISTING, search, nullptr);
  const bool removed = found != nullptr && delete_certificate(store, found);
  if (found != nullptr) {
    CertFreeCertificateContext(found);
  }
  CertFreeCertificateContext(search);
  CertCloseStore(store, 0);
  if (!removed) {
    detail::set_last_error("CA certificate was not found in the ROOT store");
  }
  return removed;
}

auto remove_stale_ca_certificates(std::string_view common_name,
                                  std::span<const std::byte> keep_der) -> bool {
  HCERTSTORE store = open_root_store();
  if (store == nullptr) {
    detail::set_last_error(
        "cannot open the LocalMachine ROOT store, run as administrator");
    return false;
  }
  PCCERT_CONTEXT current = nullptr;
  bool ok = true;
  while ((current = CertEnumCertificatesInStore(store, current)) != nullptr) {
    if (!detail::same_hostname(common_name_of(current), common_name) ||
        same_der(current, keep_der)) {
      continue;
    }
    if (!delete_certificate(store, current)) {
      ok = false;
    }
  }
  CertCloseStore(store, 0);
  if (!ok) {
    detail::set_last_error("a stale proxy CA could not be removed");
  }
  return ok;
}

auto device_info() -> DeviceInfo {
  DeviceInfo info;
  char host[256]{};
  DWORD host_size = sizeof host;
  if (GetComputerNameA(host, &host_size)) {
    info.hostname = host;
  }
  info.platform = "windows";
#if defined(_M_X64)
  info.arch = "x64";
#elif defined(_M_IX86)
  info.arch = "x86";
#elif defined(_M_ARM64)
  info.arch = "arm64";
#elif defined(_M_ARM)
  info.arch = "arm";
#else
  info.arch = "unknown";
#endif
  constexpr const char *kVersionKey =
      "SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion";
  char product[256]{};
  DWORD product_size = sizeof product;
  if (RegGetValueA(HKEY_LOCAL_MACHINE, kVersionKey, "ProductName",
                   RRF_RT_REG_SZ, nullptr, product,
                   &product_size) == ERROR_SUCCESS) {
    info.os = product;
    char display[64]{};
    DWORD display_size = sizeof display;
    if (RegGetValueA(HKEY_LOCAL_MACHINE, kVersionKey, "DisplayVersion",
                     RRF_RT_REG_SZ, nullptr, display,
                     &display_size) == ERROR_SUCCESS) {
      info.os += " ";
      info.os += display;
    }
  } else {
    info.os = "Windows";
  }
  SYSTEM_INFO system{};
  GetSystemInfo(&system);
  info.cpu_count = system.dwNumberOfProcessors;
  return info;
}

} // namespace spx::platform
