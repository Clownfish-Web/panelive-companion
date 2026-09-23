#include "windows_vmix_streaming_destination_source.hpp"

#include <Windows.h>
#include <objidl.h>
#include <xmllite.h>

#include <algorithm>
#include <cctype>
#include <charconv>
#include <fstream>
#include <limits>
#include <string_view>
#include <wrl/client.h>

namespace studio_remote::windows
{
namespace
{

using Microsoft::WRL::ComPtr;

constexpr std::uintmax_t maximum_settings_bytes = 4U << 20U;
constexpr std::uint32_t maximum_stream_destinations = 5;

struct ProfileSettings
{
  std::vector<std::string> profiles;
  std::size_t selected = 0;
};

struct DestinationFields
{
  std::string url;
  std::string provider;
  std::string name_or_key;
};

std::filesystem::path default_local_app_data()
{
  const auto length = GetEnvironmentVariableW(L"LOCALAPPDATA", nullptr, 0);
  if (length == 0)
    return {};
  std::wstring value(length, L'\0');
  const auto written = GetEnvironmentVariableW(L"LOCALAPPDATA", value.data(), length);
  if (written == 0 || written >= length)
    return {};
  value.resize(written);
  return value;
}

std::filesystem::path current_settings_path(const std::filesystem::path& local_app_data)
{
  if (local_app_data.empty())
    return {};
  std::filesystem::path newest;
  std::filesystem::file_time_type newest_time{};
  std::error_code error;
  // Match the native geometry source's current-user settings selection. Startup backups are
  // deliberately excluded: Save and Close updates user.config without updating current.config.
  for (const auto* company : {L"StudioCoast_Pty_Ltd", L"StudioCoast Pty Ltd"})
  {
    const auto root = local_app_data / company;
    error.clear();
    std::size_t visited = 0;
    for (std::filesystem::recursive_directory_iterator entry(root, error), end;
         !error && entry != end;
         entry.increment(error))
    {
      if (++visited > 512)
        return {};
      if (entry.depth() >= 2)
        entry.disable_recursion_pending();
      if (entry.depth() != 2 || entry->path().filename() != "user.config" ||
          !entry->is_regular_file(error))
        continue;
      const auto application = entry->path().parent_path().parent_path().filename().wstring();
      if (!application.starts_with(L"vMix64.exe_") && !application.starts_with(L"vMix.exe_"))
        continue;
      const auto written = entry->last_write_time(error);
      if (!error && (newest.empty() || written > newest_time))
      {
        newest = entry->path();
        newest_time = written;
      }
    }
  }
  return newest;
}

ComPtr<IStream> stream_for(std::string_view xml)
{
  if (xml.size() > static_cast<std::size_t>(std::numeric_limits<UINT>::max()))
    return {};
  const auto memory = GlobalAlloc(GMEM_MOVEABLE, xml.size());
  if (!memory)
    return {};
  const auto bytes = GlobalLock(memory);
  if (!bytes)
  {
    GlobalFree(memory);
    return {};
  }
  std::copy(xml.begin(), xml.end(), static_cast<char*>(bytes));
  GlobalUnlock(memory);

  ComPtr<IStream> stream;
  if (FAILED(CreateStreamOnHGlobal(memory, TRUE, &stream)))
  {
    GlobalFree(memory);
    return {};
  }
  return stream;
}

ComPtr<IXmlReader> reader_for(std::string_view xml)
{
  auto stream = stream_for(xml);
  if (!stream)
    return {};
  ComPtr<IXmlReader> reader;
  if (FAILED(CreateXmlReader(__uuidof(IXmlReader), &reader, nullptr)) ||
      FAILED(reader->SetInput(stream.Get())))
    return {};
  reader->SetProperty(XmlReaderProperty_DtdProcessing, DtdProcessing_Prohibit);
  return reader;
}

std::wstring_view local_name(IXmlReader& reader)
{
  const wchar_t* value = nullptr;
  UINT length = 0;
  return SUCCEEDED(reader.GetLocalName(&value, &length)) && value ? std::wstring_view(value, length)
                                                                  : std::wstring_view{};
}

std::wstring_view node_value(IXmlReader& reader)
{
  const wchar_t* value = nullptr;
  UINT length = 0;
  return SUCCEEDED(reader.GetValue(&value, &length)) && value ? std::wstring_view(value, length)
                                                              : std::wstring_view{};
}

std::string utf8(std::wstring_view value)
{
  if (value.empty())
    return {};
  const auto size = WideCharToMultiByte(CP_UTF8,
                                        WC_ERR_INVALID_CHARS,
                                        value.data(),
                                        static_cast<int>(value.size()),
                                        nullptr,
                                        0,
                                        nullptr,
                                        nullptr);
  if (size <= 0)
    return {};
  std::string result(static_cast<std::size_t>(size), '\0');
  if (WideCharToMultiByte(CP_UTF8,
                          WC_ERR_INVALID_CHARS,
                          value.data(),
                          static_cast<int>(value.size()),
                          result.data(),
                          size,
                          nullptr,
                          nullptr) != size)
    return {};
  return result;
}

std::wstring attribute(IXmlReader& reader, const wchar_t* name)
{
  if (FAILED(reader.MoveToAttributeByName(name, nullptr)))
    return {};
  const auto value = node_value(reader);
  std::wstring result(value);
  reader.MoveToElement();
  return result;
}

bool text_node(XmlNodeType type)
{
  return type == XmlNodeType_Text || type == XmlNodeType_CDATA || type == XmlNodeType_Whitespace;
}

std::optional<ProfileSettings> read_profiles(std::string_view settings_xml)
{
  auto reader = reader_for(settings_xml);
  if (!reader)
    return std::nullopt;

  ProfileSettings result;
  bool profiles_seen = false;
  std::wstring current_setting;
  enum class Capture
  {
    none,
    profile,
    selected
  } capture = Capture::none;
  std::string captured;

  XmlNodeType type{};
  HRESULT read_result = S_OK;
  while ((read_result = reader->Read(&type)) == S_OK)
  {
    const auto name = local_name(*reader.Get());
    if (type == XmlNodeType_Element && name == L"setting")
    {
      current_setting = attribute(*reader.Get(), L"name");
      profiles_seen = profiles_seen || current_setting == L"FMLESettings";
    }
    else if (type == XmlNodeType_Element && current_setting == L"FMLESettings" && name == L"string")
    {
      captured.clear();
      if (reader->IsEmptyElement())
        result.profiles.emplace_back();
      else
        capture = Capture::profile;
    }
    else if (type == XmlNodeType_Element && current_setting == L"FMLESettingsSelected" &&
             name == L"value")
    {
      capture = Capture::selected;
      captured.clear();
    }
    else if (text_node(type) && capture != Capture::none)
      captured += utf8(node_value(*reader.Get()));
    else if (type == XmlNodeType_EndElement &&
             ((capture == Capture::profile && name == L"string") ||
              (capture == Capture::selected && name == L"value")))
    {
      if (capture == Capture::profile)
        result.profiles.push_back(std::move(captured));
      else
      {
        std::size_t selected = 0;
        const auto parsed =
            std::from_chars(captured.data(), captured.data() + captured.size(), selected);
        if (parsed.ec == std::errc{} && parsed.ptr == captured.data() + captured.size())
          result.selected = selected;
        else
          return std::nullopt;
      }
      capture = Capture::none;
    }
    if (type == XmlNodeType_EndElement && name == L"setting")
      current_setting.clear();
  }
  if (read_result != S_FALSE || !profiles_seen)
    return std::nullopt;
  if (result.profiles.empty())
    return result;
  if (result.selected >= result.profiles.size())
    return std::nullopt;
  return result;
}

std::optional<std::vector<std::pair<std::uint32_t, std::string>>>
destination_fragments(std::string_view profile)
{
  const auto document = std::string("<root>") + std::string(profile) + "</root>";
  auto reader = reader_for(document);
  if (!reader)
    return {};

  std::vector<std::pair<std::uint32_t, std::string>> result;
  std::optional<std::uint32_t> stream;
  std::wstring capture_name;
  std::string captured;
  XmlNodeType type{};
  HRESULT read_result = S_OK;
  while ((read_result = reader->Read(&type)) == S_OK)
  {
    const auto name = local_name(*reader.Get());
    if (type == XmlNodeType_Element && name.starts_with(L"Destination"))
    {
      std::uint32_t value = 0;
      const auto digits = name.substr(std::wstring_view(L"Destination").size());
      const auto narrow_digits = utf8(digits);
      const auto parsed =
          std::from_chars(narrow_digits.data(), narrow_digits.data() + narrow_digits.size(), value);
      if (parsed.ec == std::errc{} && parsed.ptr == narrow_digits.data() + narrow_digits.size() &&
          value < maximum_stream_destinations)
      {
        if (reader->IsEmptyElement())
        {
          result.emplace_back(value, "");
          continue;
        }
        stream = value;
        capture_name = name;
        captured.clear();
      }
    }
    else if (text_node(type) && stream)
      captured += utf8(node_value(*reader.Get()));
    else if (type == XmlNodeType_EndElement && stream && name == capture_name)
    {
      result.emplace_back(*stream, std::move(captured));
      stream.reset();
    }
  }
  if (read_result != S_FALSE)
    return std::nullopt;
  return result;
}

std::optional<DestinationFields> destination_fields(std::string_view fragment)
{
  const auto document = std::string("<root>") + std::string(fragment) + "</root>";
  auto reader = reader_for(document);
  if (!reader)
    return {};

  DestinationFields result;
  std::wstring field;
  XmlNodeType type{};
  HRESULT read_result = S_OK;
  while ((read_result = reader->Read(&type)) == S_OK)
  {
    const auto name = local_name(*reader.Get());
    if (type == XmlNodeType_Element && (name == L"URL" || name == L"Provider" || name == L"Stream"))
    {
      field = reader->IsEmptyElement() ? std::wstring{} : std::wstring(name);
    }
    else if (text_node(type) && !field.empty())
    {
      const auto value = utf8(node_value(*reader.Get()));
      if (field == L"URL")
        result.url += value;
      else if (field == L"Provider")
        result.provider += value;
      else
        result.name_or_key += value;
    }
    else if (type == XmlNodeType_EndElement && !field.empty() && name == field)
      field.clear();
  }
  if (read_result != S_FALSE)
    return std::nullopt;
  return result;
}

std::string lower_ascii(std::string value)
{
  std::transform(value.begin(),
                 value.end(),
                 value.begin(),
                 [](unsigned char character)
                 { return static_cast<char>(std::tolower(character)); });
  return value;
}

std::string sanitized_host(std::string value)
{
  if (const auto scheme = value.find("://"); scheme != std::string::npos)
    value.erase(0, scheme + 3);
  if (const auto credentials = value.find('@'); credentials != std::string::npos)
    value.erase(0, credentials + 1);
  if (const auto separator = value.find_first_of("/?#"); separator != std::string::npos)
    value.erase(separator);
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())))
    value.erase(value.begin());
  while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())))
    value.pop_back();
  if (value.empty() || value.size() > 256 ||
      std::any_of(value.begin(),
                  value.end(),
                  [](unsigned char character) { return character < 0x20 || character == 0x7f; }))
    return {};
  return value;
}

std::string service_kind(std::string_view provider, std::string_view host)
{
  const auto value = lower_ascii(std::string(provider) + " " + std::string(host));
  if (value.find("youtube") != std::string::npos || value.find("youtu.be") != std::string::npos)
    return "youtube";
  if (value.find("twitch") != std::string::npos)
    return "twitch";
  if (value.find("facebook") != std::string::npos || value.find("fbcdn") != std::string::npos)
    return "facebook";
  if (value.find("kick") != std::string::npos)
    return "kick";
  return "custom";
}

std::string fallback_destination_name(std::string_view kind, std::uint32_t stream)
{
  std::string provider;
  if (kind == "youtube")
    provider = "YouTube";
  else if (kind == "twitch")
    provider = "Twitch";
  else if (kind == "facebook")
    provider = "Facebook";
  else if (kind == "kick")
    provider = "Kick";
  else
    provider = "Custom Stream";
  return provider + " " + std::to_string(stream + 1);
}

std::optional<std::vector<vmix::StreamingDestinationConfiguration>>
parse_destinations(std::string_view settings_xml)
{
  const auto profiles = read_profiles(settings_xml);
  if (!profiles)
    return std::nullopt;
  if (profiles->profiles.empty())
    return std::vector<vmix::StreamingDestinationConfiguration>{};

  std::vector<vmix::StreamingDestinationConfiguration> result;
  const auto fragments = destination_fragments(profiles->profiles[profiles->selected]);
  if (!fragments)
    return std::nullopt;
  for (const auto& [stream, fragment] : *fragments)
  {
    const auto fields = destination_fields(fragment);
    if (!fields)
      return std::nullopt;
    const auto host = sanitized_host(fields->url);
    if (lower_ascii(fields->provider) == "none" || host.empty())
      continue;
    const auto kind = service_kind(fields->provider, host);
    const auto name_or_key =
        fields->name_or_key.empty() ? fallback_destination_name(kind, stream) : fields->name_or_key;
    result.push_back({stream, name_or_key, host, kind});
  }
  std::sort(result.begin(),
            result.end(),
            [](const auto& left, const auto& right) { return left.stream < right.stream; });
  return result;
}

} // namespace

WindowsVmixStreamingDestinationSource::WindowsVmixStreamingDestinationSource(
    bool available, std::filesystem::path settings_path, std::filesystem::path local_app_data)
    : available_(available), settings_path_(std::move(settings_path)),
      local_app_data_(local_app_data.empty() ? default_local_app_data() : std::move(local_app_data))
{
}

bool WindowsVmixStreamingDestinationSource::available() const
{
  return available_.load();
}

std::optional<std::vector<vmix::StreamingDestinationConfiguration>>
WindowsVmixStreamingDestinationSource::read_destinations()
{
  if (!available())
    return std::nullopt;
  std::lock_guard lock(mutex_);
  const auto path =
      settings_path_.empty() ? current_settings_path(local_app_data_) : settings_path_;
  std::error_code error;
  const auto size = std::filesystem::file_size(path, error);
  if (error || size == 0 || size > maximum_settings_bytes)
    return std::nullopt;
  const auto write_time = std::filesystem::last_write_time(path, error);
  if (error)
    return std::nullopt;
  if (cached_destinations_ && path == cached_path_ && size == cached_size_ &&
      write_time == cached_write_time_)
    return cached_destinations_;

  std::ifstream stream(path, std::ios::binary);
  if (!stream)
    return std::nullopt;
  std::string contents(static_cast<std::size_t>(size), '\0');
  if (!stream.read(contents.data(), static_cast<std::streamsize>(contents.size())))
    return std::nullopt;
  auto destinations = parse_destinations(contents);
  if (!destinations)
    return std::nullopt;

  if (std::filesystem::last_write_time(path, error) != write_time || error ||
      std::filesystem::file_size(path, error) != size || error)
    return std::nullopt;
  cached_path_ = path;
  cached_destinations_ = std::move(destinations);
  cached_size_ = size;
  cached_write_time_ = write_time;
  return cached_destinations_;
}

void WindowsVmixStreamingDestinationSource::set_available(bool available)
{
  available_.store(available);
}

} // namespace studio_remote::windows
