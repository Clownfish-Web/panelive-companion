#pragma once

#include "../shared/pairing_qr.hpp"

#include <winrt/Microsoft.UI.Xaml.Controls.h>

namespace studio_remote::windows
{
winrt::Microsoft::UI::Xaml::Controls::Image make_pairing_image(const PairingQrImage& qr, int size);
} // namespace studio_remote::windows
