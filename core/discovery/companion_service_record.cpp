#include <studio_remote/common/utf8.hpp>
#include <studio_remote/discovery/companion_service_record.hpp>

#include <set>
#include <stdexcept>
#include <utility>

namespace studio_remote
{
namespace
{
std::string registration_type(std::string type)
{
  if (type.ends_with(".local."))
    type.resize(type.size() - 7);
  else if (type.ends_with(".local"))
    type.resize(type.size() - 6);
  if (type.empty() || type.front() != '_' || type.find('\0') != std::string::npos ||
      (!type.ends_with("._tcp") && !type.ends_with("._udp")))
    throw std::invalid_argument("DNS-SD service type is invalid");
  return type;
}

void validate_txt(const ServiceRecord& service)
{
  std::set<std::string> keys;
  std::size_t size = 0;
  for (const auto& [key, value] : service.txt)
  {
    if (key.empty() || key.size() > 254 || value.size() > 254 - key.size())
      throw std::invalid_argument("DNS-SD TXT entry exceeds its size limit");
    std::string folded;
    for (const auto character : key)
    {
      if (character < 0x20 || character > 0x7e || character == '=')
        throw std::invalid_argument("DNS-SD TXT key is invalid");
      folded += character >= 'A' && character <= 'Z' ? static_cast<char>(character + ('a' - 'A'))
                                                     : character;
    }
    if (!keys.insert(std::move(folded)).second)
      throw std::invalid_argument("DNS-SD TXT keys must be unique ignoring case");
    size += key.size() + value.size() + 2;
    if (size > 1300)
      throw std::invalid_argument("DNS-SD TXT record exceeds its size limit");
  }
}
} // namespace

ServiceRecord make_companion_service_record(const GatewayIdentity& identity,
                                            std::uint16_t port,
                                            std::string_view installed_providers)
{
  auto fingerprint = identity.certificate_fingerprint;
  const auto slash = fingerprint.find('/');
  if (slash != std::string::npos)
    fingerprint.erase(0, slash + 1);
  if (fingerprint.size() > 16)
    fingerprint.resize(16);
  std::string capabilities = "webrtc,compositions,layers,audio,outputs,thumbnails,"
                             "critical-live-control-fifo-v1,pc-hotkeys-v1";
  if (installed_providers == "companion-teleprompter")
    capabilities = "companion-teleprompter";
  else if (installed_providers.find("companion-teleprompter") != std::string_view::npos)
    capabilities += ",companion-teleprompter";
  return {identity.name,
          "_obs-mobile-companion._tcp.local",
          port,
          {{"id", identity.instance_id},
           {"name", identity.name},
           {"api", "1"},
           {"contract", "2"},
           {"app", "panelive-companion"},
           {"pairing", "required"},
           {"providers", std::string(installed_providers)},
           {"cap", std::move(capabilities)},
           {"fp", std::move(fingerprint)}}};
}

std::string validate_dns_sd_record(const ServiceRecord& record)
{
  if (!record.port || record.instance_name.empty() || record.instance_name.size() > 63 ||
      record.instance_name.find('\0') != std::string::npos)
    throw std::invalid_argument("DNS-SD instance name or port is invalid");
  if (!valid_utf8(record.instance_name))
    throw std::invalid_argument("DNS-SD instance name is not valid UTF-8");
  auto type = registration_type(record.service_type);
  validate_txt(record);
  return type;
}
} // namespace studio_remote
