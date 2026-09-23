#include "linux_crypto.hpp"

#include <array>
#include <climits>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <memory>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <openssl/x509.h>
#include <openssl/x509v3.h>
#include <stdexcept>
#include <unistd.h>

namespace studio_remote::linux::crypto
{
namespace
{
constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

struct FileCloser
{
  void operator()(std::FILE* file) const noexcept
  {
    if (file)
      std::fclose(file);
  }
};

using FilePtr = std::unique_ptr<std::FILE, FileCloser>;
using KeyPtr = std::unique_ptr<EVP_PKEY, decltype(&EVP_PKEY_free)>;
using CertificatePtr = std::unique_ptr<X509, decltype(&X509_free)>;
using ExtensionPtr = std::unique_ptr<X509_EXTENSION, decltype(&X509_EXTENSION_free)>;

std::string certificate_fingerprint(X509* certificate)
{
  std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
  unsigned int length = 0;
  if (X509_digest(certificate, EVP_sha256(), digest.data(), &length) != 1)
    throw std::runtime_error("Could not fingerprint TLS certificate");
  static constexpr char hex[] = "0123456789abcdef";
  std::string result = "sha256/";
  for (unsigned int index = 0; index < length; ++index)
  {
    result.push_back(hex[digest[index] >> 4]);
    result.push_back(hex[digest[index] & 15]);
  }
  return result;
}

void add_extension(X509* certificate, int identifier, const char* value)
{
  X509V3_CTX context{};
  X509V3_set_ctx_nodb(&context);
  X509V3_set_ctx(&context, certificate, certificate, nullptr, nullptr, 0);
  ExtensionPtr extension(
      X509V3_EXT_conf_nid(nullptr, &context, identifier, const_cast<char*>(value)),
      X509_EXTENSION_free);
  if (!extension || X509_add_ext(certificate, extension.get(), -1) != 1)
    throw std::runtime_error("Could not create TLS certificate extension");
}

void combine_pem_files(const CertificateInfo& info)
{
  std::ofstream combined(info.server_pem_path, std::ios::binary | std::ios::trunc);
  std::ifstream key(info.private_key_path, std::ios::binary);
  std::ifstream certificate(info.certificate_path, std::ios::binary);
  if (!combined || !key || !certificate)
    throw std::runtime_error("Could not create combined TLS PEM file");
  combined << key.rdbuf() << certificate.rdbuf();
  combined.close();
  std::filesystem::permissions(info.server_pem_path,
                               std::filesystem::perms::owner_read |
                                   std::filesystem::perms::owner_write,
                               std::filesystem::perm_options::replace);
}
} // namespace

std::vector<std::uint8_t> random_bytes(std::size_t count)
{
  std::vector<std::uint8_t> result(count);
  if (RAND_bytes(result.data(), static_cast<int>(result.size())) != 1)
    throw std::runtime_error("OpenSSL CSPRNG failed");
  return result;
}

std::string base64url(std::span<const std::uint8_t> bytes)
{
  std::string output;
  output.reserve((bytes.size() * 4 + 2) / 3);
  std::uint32_t accumulator = 0;
  int bits = 0;
  for (const auto byte : bytes)
  {
    accumulator = (accumulator << 8) | byte;
    bits += 8;
    while (bits >= 6)
    {
      bits -= 6;
      output.push_back(alphabet[(accumulator >> bits) & 63]);
    }
  }
  if (bits)
    output.push_back(alphabet[(accumulator << (6 - bits)) & 63]);
  return output;
}

std::string sha256_hex(std::string_view value)
{
  std::array<unsigned char, 32> digest{};
  unsigned int length = 0;
  if (EVP_Digest(value.data(), value.size(), digest.data(), &length, EVP_sha256(), nullptr) != 1)
    throw std::runtime_error("Could not hash credential");
  static constexpr char hex[] = "0123456789abcdef";
  std::string result;
  result.reserve(64);
  for (const auto byte : digest)
  {
    result.push_back(hex[byte >> 4]);
    result.push_back(hex[byte & 15]);
  }
  return result;
}

bool constant_time_equal(std::string_view left, std::string_view right)
{
  return left.size() == right.size() && CRYPTO_memcmp(left.data(), right.data(), left.size()) == 0;
}

std::string uuid_v4()
{
  auto bytes = random_bytes(16);
  bytes[6] = static_cast<std::uint8_t>((bytes[6] & 0x0f) | 0x40);
  bytes[8] = static_cast<std::uint8_t>((bytes[8] & 0x3f) | 0x80);
  char text[37]{};
  std::snprintf(text,
                sizeof(text),
                "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
                bytes[0],
                bytes[1],
                bytes[2],
                bytes[3],
                bytes[4],
                bytes[5],
                bytes[6],
                bytes[7],
                bytes[8],
                bytes[9],
                bytes[10],
                bytes[11],
                bytes[12],
                bytes[13],
                bytes[14],
                bytes[15]);
  return text;
}

CertificateInfo ensure_self_signed_certificate(const std::filesystem::path& directory,
                                               std::string_view instance_id)
{
  std::filesystem::create_directories(directory);
  std::filesystem::permissions(
      directory, std::filesystem::perms::owner_all, std::filesystem::perm_options::replace);
  CertificateInfo info{
      directory / "gateway.crt", directory / "gateway.key", directory / "gateway.pem", {}};
  if (std::filesystem::exists(info.certificate_path) &&
      std::filesystem::exists(info.private_key_path))
  {
    FilePtr file(std::fopen(info.certificate_path.c_str(), "rb"));
    if (!file)
      throw std::runtime_error("Could not read TLS certificate");
    CertificatePtr certificate(PEM_read_X509(file.get(), nullptr, nullptr, nullptr), X509_free);
    if (!certificate)
      throw std::runtime_error("Invalid TLS certificate");
    info.fingerprint = certificate_fingerprint(certificate.get());
    combine_pem_files(info);
    return info;
  }

  KeyPtr key(EVP_RSA_gen(3072), EVP_PKEY_free);
  CertificatePtr certificate(X509_new(), X509_free);
  if (!key || !certificate)
    throw std::runtime_error("TLS key generation failed");
  X509_set_version(certificate.get(), 2);
  const auto serial_bytes = random_bytes(sizeof(unsigned long));
  unsigned long serial_value = 0;
  std::memcpy(&serial_value, serial_bytes.data(), sizeof(serial_value));
  const auto serial =
      static_cast<long>((serial_value & static_cast<unsigned long>(LONG_MAX)) | 1UL);
  ASN1_INTEGER_set(X509_get_serialNumber(certificate.get()), serial);
  X509_gmtime_adj(X509_get_notBefore(certificate.get()), -300);
  X509_gmtime_adj(X509_get_notAfter(certificate.get()), 60L * 60 * 24 * 365 * 10);
  X509_set_pubkey(certificate.get(), key.get());
  auto* name = X509_get_subject_name(certificate.get());
  const auto common_name = "PaneLive Companion " + std::string(instance_id);
  X509_NAME_add_entry_by_txt(name,
                             "CN",
                             MBSTRING_UTF8,
                             reinterpret_cast<const unsigned char*>(common_name.c_str()),
                             -1,
                             -1,
                             0);
  X509_set_issuer_name(certificate.get(), name);
  add_extension(certificate.get(), NID_basic_constraints, "critical,CA:FALSE");
  add_extension(certificate.get(), NID_key_usage, "critical,digitalSignature,keyEncipherment");
  add_extension(certificate.get(), NID_ext_key_usage, "serverAuth");
  std::array<char, 256> host{};
  std::string alternative_names = "DNS:localhost,IP:127.0.0.1,IP:::1";
  if (gethostname(host.data(), host.size() - 1) == 0 && host.front() != '\0')
    alternative_names += ",DNS:" + std::string(host.data()) + ",DNS:" + host.data() + ".local";
  add_extension(certificate.get(), NID_subject_alt_name, alternative_names.c_str());
  if (X509_sign(certificate.get(), key.get(), EVP_sha256()) <= 0)
    throw std::runtime_error("TLS signing failed");

  FilePtr key_file(std::fopen(info.private_key_path.c_str(), "wb"));
  if (!key_file ||
      PEM_write_PrivateKey(key_file.get(), key.get(), nullptr, nullptr, 0, nullptr, nullptr) != 1)
    throw std::runtime_error("Could not persist TLS key");
  key_file.reset();
  FilePtr certificate_file(std::fopen(info.certificate_path.c_str(), "wb"));
  if (!certificate_file || PEM_write_X509(certificate_file.get(), certificate.get()) != 1)
    throw std::runtime_error("Could not persist TLS certificate");
  certificate_file.reset();
  std::filesystem::permissions(info.private_key_path,
                               std::filesystem::perms::owner_read |
                                   std::filesystem::perms::owner_write,
                               std::filesystem::perm_options::replace);
  info.fingerprint = certificate_fingerprint(certificate.get());
  combine_pem_files(info);
  return info;
}
} // namespace studio_remote::linux::crypto
