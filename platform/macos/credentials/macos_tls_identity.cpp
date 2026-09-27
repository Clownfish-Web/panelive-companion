#include "macos_tls_identity.hpp"

#include <array>
#include <openssl/buffer.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <openssl/ssl.h>
#include <openssl/x509v3.h>
#include <stdexcept>
#include <studio_remote/credentials/credential_store.hpp>
#include <utility>

namespace studio_remote::macos
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

std::string subject_name(std::string_view identifier)
{
  if (identifier.size() != 36)
    throw std::invalid_argument("TLS identity requires a UUID instance identifier");
  for (std::size_t index = 0; index < identifier.size(); ++index)
  {
    const auto byte = identifier[index];
    const bool dash = index == 8 || index == 13 || index == 18 || index == 23;
    if (dash ? byte != '-' : !((byte >= '0' && byte <= '9') || (byte >= 'a' && byte <= 'f')))
      throw std::invalid_argument("TLS identity requires a UUID instance identifier");
  }
  return "PaneLive Companion " + std::string(identifier);
}

std::string alternative_names(std::string_view hostname)
{
  std::string result = "DNS:localhost,IP:127.0.0.1,IP:::1";
  if (hostname.empty())
    return result;
  if (hostname.size() > 253)
    throw std::invalid_argument("TLS hostname is invalid");
  for (const auto byte : hostname)
    if (!((byte >= 'a' && byte <= 'z') || (byte >= 'A' && byte <= 'Z') ||
          (byte >= '0' && byte <= '9') || byte == '-' || byte == '.'))
      throw std::invalid_argument("TLS hostname is invalid");
  result += ",DNS:" + std::string(hostname);
  if (!hostname.ends_with(".local"))
    result += ",DNS:" + std::string(hostname) + ".local";
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

Certificate
create_certificate(EVP_PKEY& key, const std::string& common_name, const std::string& names)
{
  Certificate certificate(X509_new(), X509_free);
  require(certificate != nullptr, "TLS certificate allocation failed");
  std::array<unsigned char, 16> serial_bytes{};
  require(RAND_bytes(serial_bytes.data(), static_cast<int>(serial_bytes.size())) == 1,
          "TLS serial generation failed");
  serial_bytes[0] &= 0x7f;
  serial_bytes.back() |= 1;
  std::unique_ptr<BIGNUM, decltype(&BN_free)> serial(
      BN_bin2bn(serial_bytes.data(), static_cast<int>(serial_bytes.size()), nullptr), BN_free);
  require(serial != nullptr &&
              BN_to_ASN1_INTEGER(serial.get(), X509_get_serialNumber(certificate.get())) !=
                  nullptr &&
              X509_set_version(certificate.get(), 2) == 1 &&
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
  add_extension(*certificate, NID_subject_alt_name, names.c_str());
  require(X509_sign(certificate.get(), &key, EVP_sha256()) > 0, "TLS signing failed");
  return certificate;
}

std::string fingerprint_of(X509& certificate)
{
  std::array<unsigned char, EVP_MAX_MD_SIZE> bytes{};
  unsigned int count = 0;
  require(X509_digest(&certificate, EVP_sha256(), bytes.data(), &count) == 1 && count == 32,
          "Could not fingerprint TLS certificate");
  std::string result = "sha256/";
  constexpr char hex[] = "0123456789abcdef";
  for (unsigned int index = 0; index < count; ++index)
  {
    result += hex[bytes[index] >> 4];
    result += hex[bytes[index] & 15];
  }
  return result;
}
} // namespace

class TlsIdentity::Impl
{
public:
  Key key{nullptr, EVP_PKEY_free};
  Certificate certificate{nullptr, X509_free};
  std::string fingerprint;

  void validate(const std::string& common_name)
  {
    require(key && certificate && X509_check_private_key(certificate.get(), key.get()) == 1 &&
                X509_verify(certificate.get(), key.get()) == 1 &&
                X509_NAME_cmp(X509_get_subject_name(certificate.get()),
                              X509_get_issuer_name(certificate.get())) == 0,
            "Stored TLS identity is invalid or mismatched");
    std::array<char, 128> subject{};
    const auto count = X509_NAME_get_text_by_NID(
        X509_get_subject_name(certificate.get()), NID_commonName, subject.data(), subject.size());
    require(count >= 0 && static_cast<std::size_t>(count) == common_name.size() &&
                std::string_view(subject.data(), static_cast<std::size_t>(count)) == common_name,
            "Stored TLS identity belongs to another instance");
    require(X509_cmp_current_time(X509_get0_notBefore(certificate.get())) < 0 &&
                X509_cmp_current_time(X509_get0_notAfter(certificate.get())) > 0,
            "Stored TLS identity is not currently valid; explicit recovery is required");
    fingerprint = fingerprint_of(*certificate);
  }
};

TlsIdentity TlsIdentity::load_or_create(ICredentialStore& credentials,
                                        std::string_view instance_id,
                                        std::string_view local_hostname)
{
  const auto common_name = subject_name(instance_id);
  auto impl = std::make_unique<Impl>();
  SecretPem stored{credentials.get("tls-identity")};
  if (stored.value.empty())
  {
    const auto names = alternative_names(local_hostname);
    impl->key.reset(EVP_RSA_gen(3072));
    require(impl->key != nullptr, "TLS key generation failed");
    impl->certificate = create_certificate(*impl->key, common_name, names);
    impl->validate(common_name);
    auto encoded = memory_bio();
    require(PEM_write_bio_PrivateKey(
                encoded.get(), impl->key.get(), nullptr, nullptr, 0, nullptr, nullptr) == 1 &&
                PEM_write_bio_X509(encoded.get(), impl->certificate.get()) == 1,
            "Could not encode TLS identity");
    char* bytes = nullptr;
    const auto count = BIO_get_mem_data(encoded.get(), &bytes);
    require(count > 0, "Could not encode TLS identity");
    credentials.put("tls-identity", std::string_view(bytes, static_cast<std::size_t>(count)));
  }
  else
  {
    require(stored.value.size() <= 64 * 1024, "Stored TLS identity exceeds the maximum size");
    auto decoded = memory_bio();
    require(BIO_write(decoded.get(), stored.value.data(), static_cast<int>(stored.value.size())) ==
                static_cast<int>(stored.value.size()),
            "Could not decode TLS identity");
    // A noninteractive password callback rejects encrypted or malformed unexpected input.
    impl->key.reset(PEM_read_bio_PrivateKey(
        decoded.get(), nullptr, [](char*, int, int, void*) { return 0; }, nullptr));
    impl->certificate.reset(PEM_read_bio_X509(decoded.get(), nullptr, nullptr, nullptr));
    impl->validate(common_name);
  }
  return TlsIdentity(std::move(impl));
}

TlsIdentity::TlsIdentity(std::unique_ptr<Impl> impl) : impl_(std::move(impl))
{
}
TlsIdentity::~TlsIdentity() = default;
TlsIdentity::TlsIdentity(TlsIdentity&&) noexcept = default;
TlsIdentity& TlsIdentity::operator=(TlsIdentity&&) noexcept = default;

const std::string& TlsIdentity::fingerprint() const noexcept
{
  return impl_->fingerprint;
}

void TlsIdentity::configure_server_context(ssl_ctx_st& context) const
{
  require(SSL_CTX_use_certificate(&context, impl_->certificate.get()) == 1 &&
              SSL_CTX_use_PrivateKey(&context, impl_->key.get()) == 1 &&
              SSL_CTX_check_private_key(&context) == 1,
          "Could not configure the TLS server identity");
}
} // namespace studio_remote::macos
