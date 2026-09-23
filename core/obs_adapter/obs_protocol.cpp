#include <array>
#include <openssl/evp.h>
#include <studio_remote/obs/obs_adapter.hpp>

namespace studio_remote
{
// OBS WebSocket v5 authentication is kept here so socket transports remain replaceable.
std::string obs_websocket_authentication(std::string_view password,
                                         std::string_view salt,
                                         std::string_view challenge)
{
  auto digest = [](std::string_view value)
  {
    std::array<unsigned char, 32> bytes{};
    unsigned int size = 0;
    EVP_Digest(value.data(), value.size(), bytes.data(), &size, EVP_sha256(), nullptr);
    std::string encoded(4 * ((size + 2) / 3), '\0');
    encoded.resize(
        EVP_EncodeBlock(reinterpret_cast<unsigned char*>(encoded.data()), bytes.data(), size));
    return encoded;
  };
  return digest(digest(std::string(password) + std::string(salt)) + std::string(challenge));
}
} // namespace studio_remote
