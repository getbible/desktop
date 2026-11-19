#include <gui/choc_DesktopWindow.h>
#include "webview_navigation_windows.hpp"
#include "webview_navigation.hpp"

#include <WebView2EnvironmentOptions.h>
#include <wrl/event.h>

namespace webapp::navigation
{
using Microsoft::WRL::Callback;
using Microsoft::WRL::ComPtr;
using Microsoft::WRL::Make;

HRESULT createWindowsEnvironmentOptions(IUnknown** result)
{
    if (result == nullptr) return E_POINTER;
    *result = nullptr;
    auto options = Make<CoreWebView2EnvironmentOptions>();
    auto shell = Make<CoreWebView2CustomSchemeRegistration>(L"webapp");
    if (!options || !shell) return E_OUTOFMEMORY;

    // The shell uses webapp://app for its startup, offline, About and Support
    // pages. WebView2 needs this registration before it will request them.
    HRESULT status = shell->put_HasAuthorityComponent(TRUE);
    if (FAILED(status)) return status;
    status = shell->put_TreatAsSecure(TRUE);
    if (FAILED(status)) return status;
    ComPtr<ICoreWebView2EnvironmentOptions4> schemeOptions;
    status = options.As(&schemeOptions);
    if (FAILED(status)) return status;
    ICoreWebView2CustomSchemeRegistration* registrations[] = {shell.Get()};
    status = schemeOptions->SetCustomSchemeRegistrations(1, registrations);
    if (FAILED(status)) return status;

    // Custom scheme registration has been available since WebView2 Runtime 110.
    ComPtr<ICoreWebView2EnvironmentOptions> baseOptions;
    status = options.As(&baseOptions);
    if (FAILED(status)) return status;
    status = baseOptions->put_TargetCompatibleBrowserVersion(L"110.0.1587.40");
    if (SUCCEEDED(status)) *result = baseOptions.Detach();
    return status;
}

struct EventURI
{
    LPWSTR value = nullptr;
    ~EventURI() { CoTaskMemFree(value); }

    bool allowed(HRESULT result) const
    {
        return SUCCEEDED(result) && value != nullptr
            && allowsTopLevelURL(choc::ui::createUTF8FromUTF16(value));
    }
};

bool installWindowsNavigationPolicy(IUnknown* browser)
{
    ComPtr<ICoreWebView2> view;
    if (FAILED(browser->QueryInterface(IID_PPV_ARGS(view.GetAddressOf()))))
        return false;

    auto starting = Callback<ICoreWebView2NavigationStartingEventHandler>(
        [](ICoreWebView2*, ICoreWebView2NavigationStartingEventArgs* args) -> HRESULT {
            if (args == nullptr) return E_POINTER;
            EventURI uri;
            return uri.allowed(args->get_Uri(&uri.value)) ? S_OK : args->put_Cancel(TRUE);
        });
    auto newWindow = Callback<ICoreWebView2NewWindowRequestedEventHandler>(
        [](ICoreWebView2* sender, ICoreWebView2NewWindowRequestedEventArgs* args) -> HRESULT {
            if (args == nullptr) return E_POINTER;
            const HRESULT handled = args->put_Handled(TRUE);
            if (FAILED(handled)) return handled;
            EventURI uri;
            if (sender != nullptr && uri.allowed(args->get_Uri(&uri.value)))
                return sender->Navigate(uri.value);
            return S_OK;
        });

    // WebView2 owns these stateless callbacks for the life of the controller.
    // It raises NavigationStarting again for each server redirect.
    EventRegistrationToken token{};
    return starting && newWindow
        && SUCCEEDED(view->add_NavigationStarting(starting.Get(), &token))
        && SUCCEEDED(view->add_NewWindowRequested(newWindow.Get(), &token));
}
} // namespace webapp::navigation
