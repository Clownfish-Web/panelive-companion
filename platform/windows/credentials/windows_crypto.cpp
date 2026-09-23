#include "windows_crypto.hpp"

#include <Windows.h>
#include <bcrypt.h>
#include <dpapi.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>

#include <array>
#include <limits>
#include <stdexcept>

namespace studio_remote::windows::WindowsCrypto
{
namespace
{
constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

struct ProtectedBlob
{
  DATA_BLOB data{};
  ~ProtectedBlob()
  {
    if (data.pbData)
    {
      SecureZeroMemory(data.pbData, data.cbData);
      LocalFree(data.pbData);
    }
  }
};

} // namespace

std::vector<uint8_t> random_bytes(size_t count)
{
  std::vector<uint8_t> result(count);
  if (BCryptGenRandom(nullptr,
                      result.data(),
                      static_cast<ULONG>(result.size()),
                      BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0)
    throw std::runtime_error("Windows CSPRNG failed");
  return result;
}

std::string base64url(std::span<const uint8_t> bytes)
{
  std::string output;
  output.reserve((bytes.size() * 4 + 2) / 3);
  uint32_t accumulator = 0;
  int bits = 0;
  for (uint8_t byte : bytes)
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

std::vector<uint8_t> base64url_decode(std::string_view text)
{
  std::array<int8_t, 256> table{};
  table.fill(-1);
  for (int i = 0; i < 64; ++i)
    table[static_cast<uint8_t>(alphabet[i])] = static_cast<int8_t>(i);
  std::vector<uint8_t> output;
  uint32_t accumulator = 0;
  int bits = 0;
  for (unsigned char character : text)
  {
    if (table[character] < 0)
      throw std::invalid_argument("Invalid base64url");
    accumulator = (accumulator << 6) | table[character];
    bits += 6;
    if (bits >= 8)
    {
      bits -= 8;
      output.push_back(static_cast<uint8_t>((accumulator >> bits) & 255));
    }
  }
  return output;
}

std::string sha256_hex(std::string_view value)
{
  std::array<unsigned char, 32> digest{};
  unsigned int length = 0;
  EVP_Digest(value.data(), value.size(), digest.data(), &length, EVP_sha256(), nullptr);
  static constexpr char hex[] = "0123456789abcdef";
  std::string result;
  result.reserve(64);
  for (auto byte : digest)
  {
    result.push_back(hex[byte >> 4]);
    result.push_back(hex[byte & 15]);
  }
  return result;
}

std::string hmac_sha256_base64url(std::span<const uint8_t> key, std::string_view value)
{
  std::array<unsigned char, EVP_MAX_MD_SIZE> digest{};
  unsigned int length = 0;
  HMAC(EVP_sha256(),
       key.data(),
       static_cast<int>(key.size()),
       reinterpret_cast<const unsigned char*>(value.data()),
       value.size(),
       digest.data(),
       &length);
  return base64url(std::span(digest.data(), length));
}

bool constant_time_equal(std::string_view left, std::string_view right)
{
  if (left.size() != right.size())
    return false;
  return CRYPTO_memcmp(left.data(), right.data(), left.size()) == 0;
}

std::string uuid_v4()
{
  auto bytes = random_bytes(16);
  bytes[6] = static_cast<uint8_t>((bytes[6] & 0x0f) | 0x40);
  bytes[8] = static_cast<uint8_t>((bytes[8] & 0x3f) | 0x80);
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

std::string dpapi_protect(std::string_view plain)
{
  if (plain.size() > std::numeric_limits<DWORD>::max())
    throw std::invalid_argument("DPAPI input is too large");
  DATA_BLOB input{static_cast<DWORD>(plain.size()),
                  reinterpret_cast<BYTE*>(const_cast<char*>(plain.data()))};
  ProtectedBlob output;
  if (!CryptProtectData(&input,
                        L"OBS Mobile Companion",
                        nullptr,
                        nullptr,
                        nullptr,
                        CRYPTPROTECT_UI_FORBIDDEN,
                        &output.data))
    throw std::runtime_error("DPAPI encryption failed");
  return base64url(std::span(output.data.pbData, output.data.cbData));
}

std::string dpapi_unprotect(std::string_view protected_value)
{
  auto encrypted = base64url_decode(protected_value);
  if (encrypted.size() > std::numeric_limits<DWORD>::max())
    throw std::invalid_argument("DPAPI input is too large");
  DATA_BLOB input{static_cast<DWORD>(encrypted.size()), encrypted.data()};
  ProtectedBlob output;
  if (!CryptUnprotectData(
          &input, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output.data))
    throw std::runtime_error("DPAPI decryption failed");
  return std::string(reinterpret_cast<char*>(output.data.pbData), output.data.cbData);
}

} // namespace studio_remote::windows::WindowsCrypto
