#pragma once

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>

struct ssl_ctx_st;

namespace studio_remote::macos
{
/// User-only TLS files, matching Windows/Linux. Creation/loading belongs to the configuration
/// worker. The identity never rotates implicitly on error; legacy Keychain items are not read.
class TlsIdentity
{
public:
  static TlsIdentity load_or_create(const std::filesystem::path& directory,
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
