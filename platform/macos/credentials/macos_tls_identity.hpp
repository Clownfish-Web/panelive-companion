#pragma once

#include <memory>
#include <string>
#include <string_view>

struct ssl_ctx_st;

namespace studio_remote
{
class ICredentialStore;
}

namespace studio_remote::macos
{
/// Keychain-backed server identity. Creation/loading belongs to the single configuration worker.
/// No unencrypted private-key file is created. The identity never rotates implicitly on error.
class TlsIdentity
{
public:
  static TlsIdentity load_or_create(ICredentialStore& credentials,
                                    std::string_view instance_id,
                                    std::string_view local_hostname);
  ~TlsIdentity();
  TlsIdentity(TlsIdentity&&) noexcept;
  TlsIdentity& operator=(TlsIdentity&&) noexcept;
  TlsIdentity(const TlsIdentity&) = delete;
  TlsIdentity& operator=(const TlsIdentity&) = delete;

  /// Public Windows/mobile pin format: sha256/ followed by 64 lowercase DER-digest hex digits.
  const std::string& fingerprint() const noexcept;
  /// Native TLS adapter boundary: installs reference-counted identity objects into this context.
  /// Call before starting the listener. The context may outlive this identity.
  void configure_server_context(ssl_ctx_st& context) const;

private:
  class Impl;
  explicit TlsIdentity(std::unique_ptr<Impl> impl);
  std::unique_ptr<Impl> impl_;
};
} // namespace studio_remote::macos
