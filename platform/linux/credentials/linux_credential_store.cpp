#include "linux_credential_store.hpp"

#include "linux_crypto.hpp"

#include <libsecret/secret.h>
#include <stdexcept>

namespace studio_remote::linux
{
namespace
{
const SecretSchema credential_schema = {"com.clownfishweb.panelive.companion",
                                        SECRET_SCHEMA_NONE,
                                        {{"instance", SECRET_SCHEMA_ATTRIBUTE_STRING},
                                         {"key", SECRET_SCHEMA_ATTRIBUTE_STRING},
                                         {nullptr, SECRET_SCHEMA_ATTRIBUTE_STRING}},
                                        0,
                                        nullptr,
                                        nullptr,
                                        nullptr,
                                        nullptr,
                                        nullptr,
                                        nullptr,
                                        nullptr};

std::runtime_error secret_error(std::string_view operation, GError* error)
{
  std::string message(operation);
  if (error && error->message)
    message += ": " + std::string(error->message);
  if (error)
    g_error_free(error);
  return std::runtime_error(message);
}
} // namespace

LinuxCredentialStore::LinuxCredentialStore(std::string instance_id)
    : instance_id_(std::move(instance_id))
{
  if (instance_id_.empty())
    throw std::invalid_argument("Credential store instance identifier is required");
}

std::string LinuxCredentialStore::random_token(std::size_t bytes)
{
  return crypto::base64url(crypto::random_bytes(bytes));
}

std::string LinuxCredentialStore::hash(std::string_view value)
{
  return crypto::sha256_hex(value);
}

bool LinuxCredentialStore::constant_time_equal(std::string_view left, std::string_view right)
{
  return crypto::constant_time_equal(left, right);
}

std::string LinuxCredentialStore::uuid_v4()
{
  return crypto::uuid_v4();
}

void LinuxCredentialStore::put(std::string_view key, std::string_view value)
{
  const auto key_text = std::string(key);
  const auto value_text = std::string(value);
  GError* error = nullptr;
  const auto stored = secret_password_store_sync(&credential_schema,
                                                 SECRET_COLLECTION_DEFAULT,
                                                 "PaneLive Companion credential",
                                                 value_text.c_str(),
                                                 nullptr,
                                                 &error,
                                                 "instance",
                                                 instance_id_.c_str(),
                                                 "key",
                                                 key_text.c_str(),
                                                 nullptr);
  if (!stored)
    throw secret_error("Could not store credential in Secret Service", error);
}

std::string LinuxCredentialStore::get(std::string_view key)
{
  const auto key_text = std::string(key);
  GError* error = nullptr;
  auto* password = secret_password_lookup_sync(&credential_schema,
                                               nullptr,
                                               &error,
                                               "instance",
                                               instance_id_.c_str(),
                                               "key",
                                               key_text.c_str(),
                                               nullptr);
  if (error)
    throw secret_error("Could not read credential from Secret Service", error);
  if (!password)
    return {};
  std::string result(password);
  secret_password_free(password);
  return result;
}
} // namespace studio_remote::linux
