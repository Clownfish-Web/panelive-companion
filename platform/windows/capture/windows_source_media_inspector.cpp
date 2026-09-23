#include "windows_source_media_inspector.hpp"

#include <Windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <wrl/client.h>

#include <string>
#include <utility>

namespace studio_remote::windows
{
namespace
{
using Microsoft::WRL::ComPtr;

class ScopedComInitialization
{
public:
  ScopedComInitialization()
  {
    const auto result = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    initialized_ = SUCCEEDED(result);
    usable_ = initialized_ || result == RPC_E_CHANGED_MODE;
  }

  ~ScopedComInitialization()
  {
    if (initialized_)
      CoUninitialize();
  }

  bool usable() const noexcept
  {
    return usable_;
  }

private:
  bool initialized_ = false;
  bool usable_ = false;
};

class ScopedMediaSource
{
public:
  explicit ScopedMediaSource(ComPtr<IMFMediaSource> source) : source_(std::move(source))
  {
  }

  ~ScopedMediaSource()
  {
    if (source_)
      static_cast<void>(source_->Shutdown());
  }

  IMFMediaSource* operator->() const noexcept
  {
    return source_.Get();
  }

private:
  ComPtr<IMFMediaSource> source_;
};

std::wstring wide(std::string_view value)
{
  if (value.empty())
    return {};
  const auto size = MultiByteToWideChar(
      CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), nullptr, 0);
  if (size <= 0)
    return {};
  std::wstring result(static_cast<std::size_t>(size), L'\0');
  if (MultiByteToWideChar(CP_UTF8,
                          MB_ERR_INVALID_CHARS,
                          value.data(),
                          static_cast<int>(value.size()),
                          result.data(),
                          size) != size)
    return {};
  return result;
}
} // namespace

WindowsSourceMediaInspector::WindowsSourceMediaInspector()
{
  media_foundation_started_ = SUCCEEDED(MFStartup(MF_VERSION, MFSTARTUP_LITE));
}

WindowsSourceMediaInspector::~WindowsSourceMediaInspector()
{
  if (media_foundation_started_)
    MFShutdown();
}

SourceVisualCapability WindowsSourceMediaInspector::inspect(const LocalMediaSource& source)
{
  if (!media_foundation_started_)
    return SourceVisualCapability::unknown;

  ScopedComInitialization com;
  const auto path = wide(source.path);
  if (!com.usable() || path.empty())
    return SourceVisualCapability::unknown;

  ComPtr<IMFSourceResolver> resolver;
  if (FAILED(MFCreateSourceResolver(&resolver)))
    return SourceVisualCapability::unknown;

  MF_OBJECT_TYPE object_type = MF_OBJECT_INVALID;
  ComPtr<IUnknown> object;
  if (FAILED(resolver->CreateObjectFromURL(
          path.c_str(), MF_RESOLUTION_MEDIASOURCE, nullptr, &object_type, &object)) ||
      object_type != MF_OBJECT_MEDIASOURCE)
    return SourceVisualCapability::unknown;

  ComPtr<IMFMediaSource> media_source;
  if (FAILED(object.As(&media_source)))
    return SourceVisualCapability::unknown;
  ScopedMediaSource scoped_media_source(std::move(media_source));

  ComPtr<IMFPresentationDescriptor> presentation;
  if (FAILED(scoped_media_source->CreatePresentationDescriptor(&presentation)))
    return SourceVisualCapability::unknown;

  DWORD stream_count = 0;
  if (FAILED(presentation->GetStreamDescriptorCount(&stream_count)))
    return SourceVisualCapability::unknown;

  bool has_audio = false;
  bool ambiguous = false;
  bool has_video = false;
  for (DWORD stream_index = 0; stream_index < stream_count; ++stream_index)
  {
    BOOL selected = FALSE;
    ComPtr<IMFStreamDescriptor> stream;
    if (FAILED(presentation->GetStreamDescriptorByIndex(stream_index, &selected, &stream)))
    {
      ambiguous = true;
      break;
    }

    ComPtr<IMFMediaTypeHandler> media_types;
    GUID major_type{};
    if (FAILED(stream->GetMediaTypeHandler(&media_types)) ||
        FAILED(media_types->GetMajorType(&major_type)))
    {
      ambiguous = true;
      break;
    }

    if (major_type == MFMediaType_Video)
      has_video = true;
    else if (major_type == MFMediaType_Audio)
      has_audio = true;
    else
      ambiguous = true;
  }

  if (has_video)
    return SourceVisualCapability::visual;
  if (has_audio && !ambiguous)
    return SourceVisualCapability::audio_only;

  return SourceVisualCapability::unknown;
}

} // namespace studio_remote::windows
