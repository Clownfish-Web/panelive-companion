#pragma once
#include <filesystem>
#include <studio_remote/common/types.hpp>

namespace studio_remote::windows::WindowsCrypto
{
std::vector<uint8_t> random_bytes(size_t count);
std::string base64url(std::span<const uint8_t> bytes);
std::vector<uint8_t> base64url_decode(std::string_view text);
std::string sha256_hex(std::string_view value);
bool constant_time_equal(std::string_view left, std::string_view right);
std::string uuid_v4();

// Protects secrets for the current Windows user. The returned value is base64url.
std::string dpapi_protect(std::string_view plain);
std::string dpapi_unprotect(std::string_view protected_value);

struct CertificateInfo
{
  std::filesystem::path certificate_path;
  std::filesystem::path private_key_path;
  std::filesystem::path server_pem_path;
  std::string fingerprint;
};

CertificateInfo ensure_self_signed_certificate(const std::filesystem::path& directory,
                                               std::string_view instance_id);
} // namespace studio_remote::windows::WindowsCrypto
