#pragma once

#include <unknwn.h>

namespace webapp::navigation
{
// Keep CHOC's embedded loader and its WebView2 declarations isolated from
// the SDK. The options output owns one reference to the base options interface.
HRESULT createWindowsEnvironmentOptions(IUnknown** result);
bool installWindowsNavigationPolicy(IUnknown* browser);
} // namespace webapp::navigation
