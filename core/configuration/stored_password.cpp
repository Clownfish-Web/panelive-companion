#include <studio_remote/configuration/stored_password.hpp>

namespace studio_remote
{
void replace_stored_password(std::string& plaintext,
                             std::string& protected_value,
                             const std::optional<std::string>& replacement)
{
  if (replacement)
  {
    plaintext = *replacement;
    protected_value.clear();
  }
}
} // namespace studio_remote
