#include "linux_crypto.hpp"

#include "linux_private_tls_storage.hpp"

#include <chrono>
#include <climits>
#include <openssl/buffer.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rsa.h>
#include <openssl/x509v3.h>
#include <unistd.h>

#include <array>
#include <memory>
#include <stdexcept>

namespace studio_remote::linux::crypto
{
namespace
{
using Key = std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)>;
using Certificate = std::unique_ptr<X509, decltype(&X509_free)>;

void require(bool result, const char* error)
{
  if (!result)
    throw std::runtime_error(error);
}

struct BioDeleter
{
  void operator()(BIO* bio) const noexcept
  {
    BUF_MEM* memory = nullptr;
    BIO_get_mem_ptr(bio, &memory);
    if (memory && memory->data)
      OPENSSL_cleanse(memory->data, memory->max);
    BIO_free(bio);
  }
};
using Bio = std::unique_ptr<BIO, BioDeleter>;

struct SecretPem
{
  std::string value;
  ~SecretPem()
  {
    OPENSSL_cleanse(value.data(), value.size());
  }
};

Bio memory_bio()
{
  Bio result(BIO_new(BIO_s_mem()));
  require(result != nullptr, "Could not allocate TLS identity buffer");
  return result;
}

Bio read_pem(const PrivateTlsStorage& storage, const std::filesystem::path& path)
{
  SecretPem contents{storage.read(path)};
  const auto size = contents.value.size();
  auto result = memory_bio();
  require(BIO_write(result.get(), contents.value.data(), static_cast<int>(size)) ==
              static_cast<int>(size),
          "Could not decode TLS identity");
  return result;
}

void write_pem(const PrivateTlsStorage& storage, const std::filesystem::path& path, BIO& buffer)
{
  char* bytes = nullptr;
  const auto count = BIO_get_mem_data(&buffer, &bytes);
  require(count > 0, "Could not encode TLS identity");
  storage.write_new(path, std::string_view(bytes, static_cast<std::size_t>(count)));
}

Key read_key(BIO& buffer)
{
  Key key(PEM_read_bio_PrivateKey(
              &buffer, nullptr, [](char*, int, int, void*) { return 0; }, nullptr),
          EVP_PKEY_free);
  require(key != nullptr, "Stored TLS private key is invalid");
  return key;
}

Certificate read_certificate(BIO& buffer)
{
  Certificate certificate(PEM_read_bio_X509(&buffer, nullptr, nullptr, nullptr), X509_free);
  require(certificate != nullptr, "Stored TLS certificate is invalid");
  return certificate;
}

void validate_identity(X509& certificate, EVP_PKEY& key, const std::string& common_name)
{
  require(X509_check_private_key(&certificate, &key) == 1 && X509_verify(&certificate, &key) == 1 &&
              X509_NAME_cmp(X509_get_subject_name(&certificate),
                            X509_get_issuer_name(&certificate)) == 0,
          "Stored TLS identity is invalid or mismatched");
  std::array<char, 256> subject{};
  const auto count = X509_NAME_get_text_by_NID(X509_get_subject_name(&certificate),
                                               NID_commonName,
                                               subject.data(),
                                               static_cast<int>(subject.size()));
  require(count >= 0 && static_cast<std::size_t>(count) == common_name.size() &&
              std::string_view(subject.data(), static_cast<std::size_t>(count)) == common_name,
          "Stored TLS identity belongs to another instance");
  require(X509_cmp_current_time(X509_get0_notBefore(&certificate)) < 0 &&
              X509_cmp_current_time(X509_get0_notAfter(&certificate)) > 0,
          "Stored TLS identity is not currently valid; explicit recovery is required");
}

std::string fingerprint_of(X509& certificate)
{
  std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
  unsigned int length = 0;
  require(X509_digest(&certificate, EVP_sha256(), digest.data(), &length) == 1 && length == 32,
          "Could not fingerprint TLS certificate");
  std::string result = "sha256/";
  constexpr char hex[] = "0123456789abcdef";
  for (unsigned int index = 0; index < length; ++index)
  {
    result.push_back(hex[digest[index] >> 4]);
    result.push_back(hex[digest[index] & 15]);
  }
  return result;
}

void add_extension(X509& certificate, int identifier, const char* value)
{
  X509V3_CTX context{};
  X509V3_set_ctx(&context, &certificate, &certificate, nullptr, nullptr, 0);
  std::unique_ptr<X509_EXTENSION, decltype(&X509_EXTENSION_free)> extension(
      X509V3_EXT_conf_nid(nullptr, &context, identifier, value), X509_EXTENSION_free);
  require(extension != nullptr && X509_add_ext(&certificate, extension.get(), -1) == 1,
          "Could not configure TLS certificate extensions");
}

std::string alternative_names()
{
  std::string result = "DNS:localhost,IP:127.0.0.1,IP:::1";
  std::array<char, 256> host{};
  if (gethostname(host.data(), host.size() - 1) != 0 || host.front() == '\0')
    return result;
  return result + ",DNS:" + host.data() + ",DNS:" + host.data() + ".local";
}

Certificate create_certificate(EVP_PKEY& key, const std::string& common_name)
{
  Certificate certificate(X509_new(), X509_free);
  require(certificate != nullptr, "TLS certificate allocation failed");
  require(X509_set_version(certificate.get(), 2) == 1 &&
              ASN1_INTEGER_set(
                  X509_get_serialNumber(certificate.get()),
                  static_cast<long>(static_cast<unsigned long long>(
                                        std::chrono::duration_cast<std::chrono::milliseconds>(
                                            std::chrono::steady_clock::now().time_since_epoch())
                                            .count()) &
                                    LONG_MAX)) == 1 &&
              X509_gmtime_adj(X509_getm_notBefore(certificate.get()), -300) != nullptr &&
              X509_gmtime_adj(X509_getm_notAfter(certificate.get()), 60L * 60 * 24 * 365 * 10) !=
                  nullptr &&
              X509_set_pubkey(certificate.get(), &key) == 1,
          "Could not configure TLS certificate");
  auto* subject = X509_get_subject_name(certificate.get());
  require(X509_NAME_add_entry_by_txt(subject,
                                     "CN",
                                     MBSTRING_UTF8,
                                     reinterpret_cast<const unsigned char*>(common_name.data()),
                                     static_cast<int>(common_name.size()),
                                     -1,
                                     0) == 1 &&
              X509_set_issuer_name(certificate.get(), subject) == 1,
          "Could not configure TLS certificate subject");
  add_extension(*certificate, NID_basic_constraints, "critical,CA:FALSE");
  add_extension(*certificate, NID_key_usage, "critical,digitalSignature,keyEncipherment");
  add_extension(*certificate, NID_ext_key_usage, "serverAuth");
  add_extension(*certificate, NID_subject_alt_name, alternative_names().c_str());
  require(X509_sign(certificate.get(), &key, EVP_sha256()) > 0, "TLS signing failed");
  return certificate;
}

void write_combined(const PrivateTlsStorage& storage,
                    const std::filesystem::path& path,
                    X509& certificate,
                    EVP_PKEY& key)
{
  auto buffer = memory_bio();
  require(PEM_write_bio_PrivateKey(buffer.get(), &key, nullptr, nullptr, 0, nullptr, nullptr) ==
                  1 &&
              PEM_write_bio_X509(buffer.get(), &certificate) == 1,
          "Could not encode TLS identity");
  write_pem(storage, path, *buffer);
}

void validate_combined(const PrivateTlsStorage& storage,
                       const std::filesystem::path& path,
                       X509& certificate,
                       EVP_PKEY& key)
{
  auto stored = read_pem(storage, path);
  auto stored_key = read_key(*stored);
  auto stored_certificate = read_certificate(*stored);
  require(EVP_PKEY_eq(stored_key.get(), &key) == 1 &&
              X509_cmp(stored_certificate.get(), &certificate) == 0,
          "Stored TLS server identity does not match its certificate and key");
}
} // namespace

CertificateInfo ensure_self_signed_certificate(const std::filesystem::path& directory,
                                               std::string_view instance_id)
{
  require(!instance_id.empty() && instance_id.size() <= 128 &&
              instance_id.find('\0') == std::string_view::npos,
          "TLS instance identifier is invalid");
  // Preserve the current Linux subject so valid installed identities retain their pins.
  const std::string common_name = "PaneLive Companion " + std::string(instance_id);
  const PrivateTlsStorage storage(directory);
  CertificateInfo info{
      directory / "gateway.crt", directory / "gateway.key", directory / "gateway.pem", {}};
  const bool certificate_exists =
      std::filesystem::exists(std::filesystem::symlink_status(info.certificate_path));
  const bool key_exists =
      std::filesystem::exists(std::filesystem::symlink_status(info.private_key_path));
  const bool combined_exists =
      std::filesystem::exists(std::filesystem::symlink_status(info.server_pem_path));
  if (certificate_exists || key_exists || combined_exists)
  {
    require(certificate_exists && key_exists,
            "Stored TLS identity is incomplete; explicit recovery is required");
    auto certificate_buffer = read_pem(storage, info.certificate_path);
    auto key_buffer = read_pem(storage, info.private_key_path);
    auto certificate = read_certificate(*certificate_buffer);
    auto key = read_key(*key_buffer);
    validate_identity(*certificate, *key, common_name);
    if (combined_exists)
      validate_combined(storage, info.server_pem_path, *certificate, *key);
    else
      write_combined(storage, info.server_pem_path, *certificate, *key);
    info.fingerprint = fingerprint_of(*certificate);
    return info;
  }

  Key key(EVP_RSA_gen(3072), EVP_PKEY_free);
  require(key != nullptr, "TLS key generation failed");
  auto certificate = create_certificate(*key, common_name);
  validate_identity(*certificate, *key, common_name);
  auto key_buffer = memory_bio();
  require(PEM_write_bio_PrivateKey(
              key_buffer.get(), key.get(), nullptr, nullptr, 0, nullptr, nullptr) == 1,
          "Could not encode TLS key");
  auto certificate_buffer = memory_bio();
  require(PEM_write_bio_X509(certificate_buffer.get(), certificate.get()) == 1,
          "Could not encode TLS certificate");
  write_pem(storage, info.private_key_path, *key_buffer);
  write_pem(storage, info.certificate_path, *certificate_buffer);
  write_combined(storage, info.server_pem_path, *certificate, *key);
  info.fingerprint = fingerprint_of(*certificate);
  return info;
}
} // namespace studio_remote::linux::crypto
