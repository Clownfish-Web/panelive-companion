#include "linux_crypto.hpp"

#include <array>
#include <cstdio>
#include <limits>
#include <openssl/crypto.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <stdexcept>

namespace studio_remote::linux::crypto
{
namespace
{
constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

} // namespace

std::vector<std::uint8_t> random_bytes(std::size_t count)
{
  if (count > static_cast<std::size_t>(std::numeric_limits<int>::max()))
    throw std::invalid_argument("Random byte request is too large");
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
  if (EVP_Digest(value.data(), value.size(), digest.data(), &length, EVP_sha256(), nullptr) != 1 ||
      length != digest.size())
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

} // namespace studio_remote::linux::crypto
