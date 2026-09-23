#pragma once

#include <optional>
#include <string>

namespace studio_remote
{
/// An omitted replacement preserves the decrypted value and its protected fallback. An explicit
/// replacement, including an empty password, invalidates the previous protected representation.
void replace_stored_password(std::string& plaintext,
                             std::string& protected_value,
                             const std::optional<std::string>& replacement);
} // namespace studio_remote
