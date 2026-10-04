#include "macos_credential_store.hpp"

#import <CommonCrypto/CommonDigest.h>
#import <Foundation/Foundation.h>
#import <LocalAuthentication/LocalAuthentication.h>
#import <Security/Security.h>

#include <array>
#include <limits>
#include <string.h>
#include <utility>
#include <vector>

namespace studio_remote::macos
{
namespace
{
void validate_identifier(std::string_view value)
{
  if (value.empty() || value.size() > 256 || value.find('\0') != std::string_view::npos)
    throw std::invalid_argument("Credential identifier must contain 1 to 256 UTF-8 bytes");
}

NSString* identifier_text(std::string_view value)
{
  validate_identifier(value);
  NSString* text = [[NSString alloc] initWithBytes:value.data()
                                            length:value.size()
                                          encoding:NSUTF8StringEncoding];
  if (!text)
    throw std::invalid_argument("Credential identifier is not valid UTF-8");
  return text;
}

NSMutableDictionary*
credential_query(std::string_view instance, std::string_view key, KeychainInteraction interaction)
{
  // Service and account are separate attributes, so arbitrary key punctuation cannot collide.
  NSString* service =
      [@"panelive-companion/user-credentials/" stringByAppendingString:identifier_text(instance)];
  LAContext* authentication = [[LAContext alloc] init];
  authentication.interactionNotAllowed = interaction != KeychainInteraction::allow;
  return [@{
    (__bridge id)kSecClass : (__bridge id)kSecClassGenericPassword,
    (__bridge id)kSecAttrService : service,
    (__bridge id)kSecAttrAccount : identifier_text(key),
    (__bridge id)kSecAttrSynchronizable : @NO,
    (__bridge id)kSecUseAuthenticationContext : authentication
  } mutableCopy];
}

void check_keychain_status(OSStatus status)
{
  if (status == errSecSuccess)
    return;
  const auto category = status == errSecAuthFailed || status == errSecInteractionNotAllowed ||
                                status == errSecUserCanceled
                            ? KeychainFailure::access_denied
                        : status == errSecNotAvailable ? KeychainFailure::unavailable
                                                       : KeychainFailure::unexpected;
  throw KeychainError(category, status);
}

// Keep encryption at rest without per-executable confirmation, analogous to
// user DPAPI and desktop keyrings. macOS still enforces its signing-partition
// boundary. Only new Companion items receive this policy. The file-Keychain ACL
// APIs have no modern equivalent for this user-scoped policy.
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
id user_scoped_access()
{
  SecAccessRef raw_access = nullptr;
  check_keychain_status(SecAccessCreate(
      CFSTR("PaneLive Companion backend credential"), nullptr, &raw_access));
  id access = CFBridgingRelease(raw_access);
  NSArray* entries = CFBridgingRelease(SecAccessCopyMatchingACLList(
      (__bridge SecAccessRef)access, kSecACLAuthorizationDecrypt));
  if (entries.count == 0)
    throw KeychainError(KeychainFailure::unexpected, errSecDecode);
  for (id entry in entries)
  {
    // NULL here trusts applications within the user's unlocked keychain; an
    // empty array would instead trust none. The owner/change-ACL entry is left
    // untouched.
    check_keychain_status(
        SecACLSetContents((__bridge SecACLRef)entry, nullptr,
                          CFSTR("PaneLive Companion backend credential"), 0));
  }
  return access;
}
#pragma clang diagnostic pop

std::string_view error_message(KeychainFailure failure)
{
  switch (failure)
  {
  case KeychainFailure::access_denied:
    return "Credential storage is locked or access was denied. Unlock your login keychain and retry.";
  case KeychainFailure::unavailable:
    return "Keychain is unavailable. Sign in to the macOS user session and retry.";
  case KeychainFailure::unexpected:
    return "Could not access Companion credentials in Keychain.";
  }
  return "Could not access Companion credentials in Keychain.";
}
} // namespace

KeychainError::KeychainError(KeychainFailure failure, int native_status)
    : std::runtime_error(std::string(error_message(failure))), failure_(failure),
      native_status_(native_status)
{
}

KeychainFailure KeychainError::failure() const noexcept
{
  return failure_;
}

int KeychainError::native_status() const noexcept
{
  return native_status_;
}

MacosCredentialStore::MacosCredentialStore(std::string instance_id, KeychainInteraction interaction)
    : instance_id_(std::move(instance_id)), interaction_(interaction)
{
  @autoreleasepool
  {
    identifier_text(instance_id_);
  }
}

std::string MacosCredentialStore::random_token(std::size_t bytes)
{
  @autoreleasepool
  {
    std::vector<unsigned char> random(bytes);
    if (bytes && SecRandomCopyBytes(kSecRandomDefault, bytes, random.data()) != errSecSuccess)
      throw std::runtime_error("macOS CSPRNG failed");
    NSData* data = [NSData dataWithBytes:random.data() length:random.size()];
    NSString* encoded = [data base64EncodedStringWithOptions:0];
    encoded = [encoded stringByReplacingOccurrencesOfString:@"+" withString:@"-"];
    encoded = [encoded stringByReplacingOccurrencesOfString:@"/" withString:@"_"];
    encoded = [encoded stringByReplacingOccurrencesOfString:@"=" withString:@""];
    return std::string(encoded.UTF8String);
  }
}

std::string MacosCredentialStore::hash(std::string_view value)
{
  if (value.size() > std::numeric_limits<CC_LONG>::max())
    throw std::length_error("Credential exceeds SHA-256 input limit");
  std::array<unsigned char, CC_SHA256_DIGEST_LENGTH> digest{};
  CC_SHA256(value.data(), static_cast<CC_LONG>(value.size()), digest.data());
  constexpr char hex[] = "0123456789abcdef";
  std::string result;
  result.reserve(digest.size() * 2);
  for (const auto byte : digest)
  {
    result.push_back(hex[byte >> 4]);
    result.push_back(hex[byte & 15]);
  }
  return result;
}

bool MacosCredentialStore::constant_time_equal(std::string_view left, std::string_view right)
{
  return left.size() == right.size() &&
         (left.empty() || timingsafe_bcmp(left.data(), right.data(), left.size()) == 0);
}

std::string MacosCredentialStore::uuid_v4()
{
  @autoreleasepool
  {
    return std::string([NSUUID UUID].UUIDString.lowercaseString.UTF8String);
  }
}

void MacosCredentialStore::put(std::string_view key, std::string_view value)
{
  @autoreleasepool
  {
    auto* query = credential_query(instance_id_, key, interaction_);
    // Empty means cleared in ICredentialStore. The file-based macOS Keychain may treat an
    // empty-data update as unchanged, so remove the item instead of retaining an old secret.
    if (value.empty())
    {
      const auto status = SecItemDelete((__bridge CFDictionaryRef)query);
      if (status != errSecItemNotFound)
        check_keychain_status(status);
      return;
    }
    NSData* data = [NSData dataWithBytes:value.data() length:value.size()];
    NSDictionary* attributes = @{(__bridge id)kSecValueData : data};
    auto status =
        SecItemUpdate((__bridge CFDictionaryRef)query, (__bridge CFDictionaryRef)attributes);
    if (status == errSecItemNotFound)
    {
      query[(__bridge id)kSecValueData] = data;
      query[(__bridge id)kSecAttrAccess] = user_scoped_access();
      status = SecItemAdd((__bridge CFDictionaryRef)query, nullptr);
      if (status == errSecDuplicateItem)
      {
        // Another writer created the same item after our update. Preserve normal put semantics.
        [query removeObjectForKey:(__bridge id)kSecValueData];
        [query removeObjectForKey:(__bridge id)kSecAttrAccess];
        status =
            SecItemUpdate((__bridge CFDictionaryRef)query, (__bridge CFDictionaryRef)attributes);
      }
    }
    check_keychain_status(status);
  }
}

std::string MacosCredentialStore::get(std::string_view key)
{
  @autoreleasepool
  {
    auto* query = credential_query(instance_id_, key, interaction_);
    query[(__bridge id)kSecReturnData] = @YES;
    query[(__bridge id)kSecMatchLimit] = (__bridge id)kSecMatchLimitOne;
    CFTypeRef raw = nullptr;
    const auto status = SecItemCopyMatching((__bridge CFDictionaryRef)query, &raw);
    id result = CFBridgingRelease(raw);
    if (status == errSecItemNotFound)
      return {};
    check_keychain_status(status);
    if (![result isKindOfClass:[NSData class]])
      throw KeychainError(KeychainFailure::unexpected, errSecDecode);
    NSData* data = result;
    return data.length ? std::string(static_cast<const char*>(data.bytes), data.length)
                       : std::string{};
  }
}
} // namespace studio_remote::macos
