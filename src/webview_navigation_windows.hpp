#pragma once

#include <wrl/event.h>

// CHOC exposes these WebView2 event registrations with void* handlers.
// Declare the callback interfaces and the event-argument vtable slots we use
// in the order specified by the WebView2 SDK.
namespace webapp::navigation
{
struct NavigationStartingArgs : IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE get_Uri(LPWSTR*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsUserInitiated(BOOL*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_IsRedirected(BOOL*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_RequestHeaders(ICoreWebView2HttpRequestHeaders**) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_Cancel(BOOL*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_Cancel(BOOL) = 0;
};

MIDL_INTERFACE("9adbe429-f36d-432b-9ddc-f8881fbd76e3")
NavigationStartingHandler : IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2*, NavigationStartingArgs*) = 0;
};

struct NewWindowArgs : IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE get_Uri(LPWSTR*) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_NewWindow(ICoreWebView2*) = 0;
    virtual HRESULT STDMETHODCALLTYPE get_NewWindow(ICoreWebView2**) = 0;
    virtual HRESULT STDMETHODCALLTYPE put_Handled(BOOL) = 0;
};

MIDL_INTERFACE("d4c185fe-c81c-4989-97af-2d3fa7ab5651")
NewWindowHandler : IUnknown
{
    virtual HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2*, NewWindowArgs*) = 0;
};

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

inline bool installWindowsNavigationPolicy(ICoreWebView2* view)
{
    using Microsoft::WRL::Callback;
    auto starting = Callback<NavigationStartingHandler>([](ICoreWebView2*, NavigationStartingArgs* args) -> HRESULT {
        if (args == nullptr) return E_POINTER;
        EventURI uri;
        return uri.allowed(args->get_Uri(&uri.value)) ? S_OK : args->put_Cancel(TRUE);
    });
    auto newWindow = Callback<NewWindowHandler>([](ICoreWebView2* sender, NewWindowArgs* args) -> HRESULT {
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
