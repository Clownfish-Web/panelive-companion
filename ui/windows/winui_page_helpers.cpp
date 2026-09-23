#include <Windows.h>

#include "winui_page_helpers.hpp"

#include <CommDlg.h>
#include <ShlObj.h>
#include <robuffer.h>

#include <winrt/Microsoft.UI.Xaml.Markup.h>
#include <winrt/Microsoft.UI.Xaml.Media.h>

#include <algorithm>
#include <array>
#include <charconv>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>

namespace studio_remote::windows
{
namespace xaml = winrt::Microsoft::UI::Xaml;
namespace controls = winrt::Microsoft::UI::Xaml::Controls;
namespace markup = winrt::Microsoft::UI::Xaml::Markup;
namespace media = winrt::Microsoft::UI::Xaml::Media;

xaml::FrameworkElement load_xaml(std::wstring_view markup_text)
{
  const auto root =
      markup::XamlReader::Load(winrt::hstring(markup_text)).as<xaml::FrameworkElement>();
  root.RequestedTheme(xaml::ElementTheme::Dark);
  return root;
}

controls::ContentDialog make_management_dialog()
{
  const auto dialog = controls::ContentDialog();
  dialog.RequestedTheme(xaml::ElementTheme::Dark);
  return dialog;
}

media::SolidColorBrush management_background_brush()
{
  return media::SolidColorBrush(winrt::Windows::UI::Color{255, 15, 17, 21});
}

media::SolidColorBrush management_navigation_brush()
{
  return media::SolidColorBrush(winrt::Windows::UI::Color{255, 24, 27, 32});
}

media::SolidColorBrush management_separator_brush()
{
  return media::SolidColorBrush(winrt::Windows::UI::Color{255, 52, 58, 68});
}

media::FontFamily management_icon_font()
{
  // Segoe MDL2 Assets is present on supported Windows 10 and Windows 11 releases.
  return media::FontFamily(L"Segoe MDL2 Assets");
}

controls::Border make_status_badge(std::string_view text, std::string_view severity)
{
  const auto badge = controls::Border();
  badge.CornerRadius(xaml::CornerRadius{10});
  badge.Padding(xaml::Thickness{9, 3, 9, 3});
  auto color = winrt::Windows::UI::Color{255, 91, 99, 110};
  if (severity == "healthy")
    color = winrt::Windows::UI::Color{255, 38, 138, 92};
  else if (severity == "warning")
    color = winrt::Windows::UI::Color{255, 151, 100, 18};
  else if (severity == "error")
    color = winrt::Windows::UI::Color{255, 170, 56, 69};
  badge.Background(media::SolidColorBrush(color));
  const auto label = controls::TextBlock();
  label.Text(wide(text));
  label.FontSize(12);
  label.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
  badge.Child(label);
  return badge;
}

controls::TextBlock make_table_text(std::string_view text, bool muted, bool semibold)
{
  const auto label = controls::TextBlock();
  label.Text(wide(text));
  label.VerticalAlignment(xaml::VerticalAlignment::Center);
  label.TextTrimming(xaml::TextTrimming::CharacterEllipsis);
  if (semibold)
    label.FontWeight(winrt::Windows::UI::Text::FontWeights::SemiBold());
  if (muted)
  {
    label.Opacity(0.68);
  }
  return label;
}

std::wstring wide(std::string_view text)
{
  if (text.empty())
    return {};
  const int size = MultiByteToWideChar(
      CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0);
  if (size <= 0)
    return L"(invalid text)";
  std::wstring result(static_cast<std::size_t>(size), L'\0');
  MultiByteToWideChar(CP_UTF8,
                      MB_ERR_INVALID_CHARS,
                      text.data(),
                      static_cast<int>(text.size()),
                      result.data(),
                      size);
  return result;
}

std::string narrow(std::wstring_view text)
{
  if (text.empty())
    return {};
  const int size = WideCharToMultiByte(CP_UTF8,
                                       WC_ERR_INVALID_CHARS,
                                       text.data(),
                                       static_cast<int>(text.size()),
                                       nullptr,
                                       0,
                                       nullptr,
                                       nullptr);
  if (size <= 0)
    throw std::invalid_argument("Text is not valid Unicode");
  std::string result(static_cast<std::size_t>(size), '\0');
  WideCharToMultiByte(CP_UTF8,
                      WC_ERR_INVALID_CHARS,
                      text.data(),
                      static_cast<int>(text.size()),
                      result.data(),
                      size,
                      nullptr,
                      nullptr);
  return result;
}

std::optional<std::uint16_t> parse_port(const winrt::hstring& text)
{
  const auto utf8 = narrow(text);
  unsigned int value = 0;
  const auto parsed = std::from_chars(utf8.data(), utf8.data() + utf8.size(), value);
  if (parsed.ec != std::errc{} || parsed.ptr != utf8.data() + utf8.size() || value == 0 ||
      value > 65'535)
    return std::nullopt;
  return static_cast<std::uint16_t>(value);
}

std::optional<std::string> choose_directory(xaml::XamlRoot, std::string_view)
{
  IFileDialog* raw_dialog = nullptr;
  winrt::check_hresult(CoCreateInstance(
      CLSID_FileOpenDialog, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&raw_dialog)));
  winrt::com_ptr<IFileDialog> dialog;
  dialog.attach(raw_dialog);
  DWORD options = 0;
  winrt::check_hresult(dialog->GetOptions(&options));
  winrt::check_hresult(dialog->SetOptions(options | FOS_PICKFOLDERS | FOS_FORCEFILESYSTEM));
  if (dialog->Show(nullptr) == HRESULT_FROM_WIN32(ERROR_CANCELLED))
    return std::nullopt;
  winrt::com_ptr<IShellItem> item;
  winrt::check_hresult(dialog->GetResult(item.put()));
  PWSTR path = nullptr;
  winrt::check_hresult(item->GetDisplayName(SIGDN_FILESYSPATH, &path));
  const std::unique_ptr<wchar_t, decltype(&CoTaskMemFree)> owned_path(path, CoTaskMemFree);
  return narrow(owned_path.get());
}

bool save_text_file(xaml::XamlRoot, std::string_view suggested_name, std::string_view contents)
{
  std::array<wchar_t, MAX_PATH> path{};
  const auto suggested = wide(suggested_name);
  std::ranges::copy_n(suggested.begin(), std::min(suggested.size(), path.size() - 1), path.begin());
  constexpr wchar_t filter[] = L"JSON Lines (*.jsonl)\0*.jsonl\0All files (*.*)\0*.*\0\0";
  OPENFILENAMEW options{sizeof(options)};
  options.lpstrFilter = filter;
  options.lpstrFile = path.data();
  options.nMaxFile = static_cast<DWORD>(path.size());
  options.lpstrDefExt = L"jsonl";
  options.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
  if (!GetSaveFileNameW(&options))
    return false;
  std::ofstream output(std::filesystem::path(path.data()), std::ios::binary | std::ios::trunc);
  output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
  if (!output)
    throw std::runtime_error("Could not save the audio diagnostics report");
  return true;
}
} // namespace studio_remote::windows
