#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace studio_remote::linux::crypto
{
struct CertificateInfo
{
  std::filesystem::path certificate_path;
  std::filesystem::path private_key_path;
  std::filesystem::path server_pem_path;
  std::string fingerprint;
};

std::vector<std::uint8_t> random_bytes(std::size_t count);
std::string base64url(std::span<const std::uint8_t> bytes);
std::string sha256_hex(std::string_view value);
bool constant_time_equal(std::string_view left, std::string_view right);
std::string uuid_v4();
CertificateInfo ensure_self_signed_certificate(const std::filesystem::path& directory,
                                               std::string_view instance_id);
} // namespace studio_remote::linux::crypto
