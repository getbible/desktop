#pragma once

#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <utility>

#include <gui/choc_DesktopWindow.h>
#include <gui/choc_WebView.h>

namespace webapp::ui
{
// These controls belong to the native window rather than the website. They
// remain available when a login page, script, or network request has failed.
class RecoveryToolbar
{
public:
    RecoveryToolbar(choc::ui::WebView& view, choc::ui::DesktopWindow& window,
                    std::string home, std::string applicationName)
        : browser(view), homeURL(std::move(home)), appName(std::move(applicationName)),
          lifetime(std::make_shared<Lifetime>())
    {
        lifetime->owner = this;
        initialise(window);
    }

    RecoveryToolbar(const RecoveryToolbar&) = delete;
    RecoveryToolbar& operator=(const RecoveryToolbar&) = delete;

    ~RecoveryToolbar()
    {
        lifetime->owner = nullptr;
        destroy();
    }

    void* getViewHandle() const { return nativeViewHandle(); }

    void show()
    {
#if CHOC_LINUX
        gtk_widget_show_all(host);
#elif CHOC_APPLE
        choc::objc::call<void>(host, "setHidden:", static_cast<BOOL>(false));
#elif CHOC_WINDOWS
        // CHOC hides newly attached content while the parent is hidden. Child
        // windows need their own ShowWindow after the parent becomes visible.
        ShowWindow(host, SW_SHOW);
        layout();
#endif
    }

    void refresh()
    {
        if (!busy && !confirming && !browser.reload(true))
            showError("The page could not be refreshed. Please try again.");
    }

    void reset()
    {
        if (busy || confirming)
            return;

        confirming = true;
        const bool confirmed = confirmReset();
        confirming = false;
        if (!confirmed)
            return;

        busy = true;
        setBusy(true);
        std::weak_ptr<Lifetime> weakLifetime = lifetime;
        // The browser unloads the website before clearing its data, preventing
        // active scripts from immediately recreating cookies during the reset.
        browser.clearBrowsingData([weakLifetime](bool succeeded) {
            if (auto state = weakLifetime.lock(); state && state->owner != nullptr)
                state->owner->resetFinished(succeeded);
        });
    }

private:
    struct Lifetime { RecoveryToolbar* owner = nullptr; };
    choc::ui::WebView& browser;
    std::string homeURL;
    std::string appName;
    std::shared_ptr<Lifetime> lifetime;
    bool busy = false;
    bool confirming = false;

    void resetFinished(bool succeeded)
    {
        busy = false;
        setBusy(false);
        if (succeeded)
            browser.navigate(homeURL);
        else
            showError("Reset could not be completed. Please try again.");
    }

#if CHOC_LINUX
    GtkWidget* host = nullptr;
    GtkWidget* ownerWindow = nullptr;
    GtkWidget* refreshButton = nullptr;
    GtkWidget* resetButton = nullptr;
    gulong keyHandler = 0;

    void initialise(choc::ui::DesktopWindow& window)
    {
        ownerWindow = GTK_WIDGET(window.getWindowHandle());
        g_object_ref(ownerWindow);
        host = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
        g_object_ref_sink(host);
        auto* bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        gtk_container_set_border_width(GTK_CONTAINER(bar), 6);
        refreshButton = gtk_button_new_with_label("Refresh");
        resetButton = gtk_button_new_with_label("Reset");
        gtk_widget_set_tooltip_text(refreshButton, "Refresh the page");
        gtk_widget_set_tooltip_text(resetButton, "Clear saved website data and sign out");
        gtk_box_pack_start(GTK_BOX(bar), refreshButton, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(bar), resetButton, FALSE, FALSE, 0);
        // Closing the window destroys its widget tree before this C++ owner
        // leaves scope. Keep signal targets alive until they are disconnected.
        g_object_ref(refreshButton);
        g_object_ref(resetButton);
        gtk_box_pack_start(GTK_BOX(host), bar, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(host), GTK_WIDGET(browser.getViewHandle()), TRUE, TRUE, 0);
        g_signal_connect(refreshButton, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
            static_cast<RecoveryToolbar*>(data)->refresh();
        }), this);
        g_signal_connect(resetButton, "clicked", G_CALLBACK(+[](GtkButton*, gpointer data) {
            static_cast<RecoveryToolbar*>(data)->reset();
        }), this);
        keyHandler = g_signal_connect(ownerWindow, "key-press-event",
            G_CALLBACK(+[](GtkWidget*, GdkEventKey* event, gpointer data) -> gboolean {
                const bool control = (event->state & GDK_CONTROL_MASK) != 0;
                const bool otherModifier = (event->state & (GDK_MOD1_MASK | GDK_SUPER_MASK)) != 0;
                if (!otherModifier && (event->keyval == GDK_KEY_F5 ||
                    (control && (event->keyval == GDK_KEY_r || event->keyval == GDK_KEY_R))))
                {
                    static_cast<RecoveryToolbar*>(data)->refresh();
                    return TRUE;
                }
                return FALSE;
            }), this);
        gtk_widget_show_all(host);
    }

    void destroy()
    {
        if (keyHandler != 0 && g_signal_handler_is_connected(ownerWindow, keyHandler))
            g_signal_handler_disconnect(ownerWindow, keyHandler);
        g_signal_handlers_disconnect_by_data(refreshButton, this);
        g_signal_handlers_disconnect_by_data(resetButton, this);
        g_clear_object(&refreshButton);
        g_clear_object(&resetButton);
        g_clear_object(&host);
        g_clear_object(&ownerWindow);
    }

    void* nativeViewHandle() const { return host; }

    bool confirmReset()
    {
        const auto title = "Reset " + appName + "?";
        auto* dialog = gtk_message_dialog_new(GTK_WINDOW(ownerWindow),
            static_cast<GtkDialogFlags>(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
            GTK_MESSAGE_QUESTION, GTK_BUTTONS_NONE, "%s", title.c_str());
        g_object_ref_sink(dialog);
        gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog),
            "This clears saved website data and signs you out.");
        gtk_dialog_add_buttons(GTK_DIALOG(dialog), "Cancel", GTK_RESPONSE_CANCEL,
                               "Reset", GTK_RESPONSE_ACCEPT, nullptr);
        gtk_dialog_set_default_response(GTK_DIALOG(dialog), GTK_RESPONSE_CANCEL);
        const bool accepted = gtk_dialog_run(GTK_DIALOG(dialog)) == GTK_RESPONSE_ACCEPT;
        gtk_widget_destroy(dialog);
        g_object_unref(dialog);
        return accepted;
    }

    void showError(const char* message)
    {
        auto* dialog = gtk_message_dialog_new(GTK_WINDOW(ownerWindow),
            static_cast<GtkDialogFlags>(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
            GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE, "%s", message);
        g_object_ref_sink(dialog);
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
        g_object_unref(dialog);
    }

    void setBusy(bool active)
    {
        gtk_widget_set_sensitive(refreshButton, !active);
        gtk_widget_set_sensitive(resetButton, !active);
        gtk_button_set_label(GTK_BUTTON(resetButton), active ? "Resetting…" : "Reset");
        gtk_widget_set_sensitive(GTK_WIDGET(browser.getViewHandle()), !active);
    }

#elif CHOC_APPLE
    id host = nullptr;
    id refreshButton = nullptr;
    id resetButton = nullptr;
    inline static char ownerKey = 0;

    static RecoveryToolbar* getOwner(id object)
    {
        return reinterpret_cast<RecoveryToolbar*>(
            objc_getAssociatedObject(object, &ownerKey));
    }

    static Class toolbarClass()
    {
        static Class type = [] {
            auto cls = choc::objc::createDelegateClass("NSView", "WebAppRecoveryToolbar_");
            class_addMethod(cls, sel_registerName("refresh:"),
                (IMP)(+[](id object, SEL, id) {
                    if (auto* toolbar = getOwner(object)) toolbar->refresh();
                }), "v@:@");
            class_addMethod(cls, sel_registerName("reset:"),
                (IMP)(+[](id object, SEL, id) {
                    if (auto* toolbar = getOwner(object)) toolbar->reset();
                }), "v@:@");
            objc_registerClassPair(cls);
            return cls;
        }();
        return type;
    }

    static choc::objc::CGRect rect(double x, double y, double width, double height)
    {
        return {{x, y}, {width, height}};
    }

    void initialise(choc::ui::DesktopWindow&)
    {
        using namespace choc::objc;
        CHOC_AUTORELEASE_BEGIN
        host = call<id>(call<id>((id)toolbarClass(), "alloc"),
                        "initWithFrame:", rect(0, 0, 640, 480));
        objc_setAssociatedObject(host, &ownerKey,
            (CHOC_OBJC_CAST_BRIDGED id)this, OBJC_ASSOCIATION_ASSIGN);
        auto bar = call<id>(callClass<id>("NSView", "alloc"),
                           "initWithFrame:", rect(0, 436, 640, 44));
        call<void>(bar, "setAutoresizingMask:", static_cast<unsigned long>(2 | 8));
        refreshButton = addButton(bar, "Refresh", "refresh:", 10);
        resetButton = addButton(bar, "Reset", "reset:", 108);
        call<void>(refreshButton, "setKeyEquivalent:", getNSString("r"));
        call<void>(refreshButton, "setKeyEquivalentModifierMask:",
                   static_cast<unsigned long>(1UL << 20)); // Command
        call<void>(host, "addSubview:", bar);
        call<void>(bar, "release");
        id view = (CHOC_OBJC_CAST_BRIDGED id)browser.getViewHandle();
        call<void>(view, "setFrame:", rect(0, 0, 640, 436));
        call<void>(view, "setAutoresizingMask:", static_cast<unsigned long>(18));
        call<void>(host, "addSubview:", view);
        CHOC_AUTORELEASE_END
    }

    id addButton(id parent, const char* title, const char* action, double x)
    {
        using namespace choc::objc;
        auto button = call<id>(callClass<id>("NSButton", "alloc"),
                              "initWithFrame:", rect(x, 8, 90, 28));
        call<void>(button, "setTitle:", getNSString(title));
        call<void>(button, "setBezelStyle:", static_cast<unsigned long>(1));
        call<void>(button, "setTarget:", host);
        call<void>(button, "setAction:", sel_registerName(action));
        call<void>(parent, "addSubview:", button);
        call<void>(button, "release");
        return button;
    }

    void destroy()
    {
        using namespace choc::objc;
        CHOC_AUTORELEASE_BEGIN
        objc_setAssociatedObject(host, &ownerKey, nullptr, OBJC_ASSOCIATION_ASSIGN);
        call<void>(host, "removeFromSuperview");
        call<void>(host, "release");
        CHOC_AUTORELEASE_END
    }

    void* nativeViewHandle() const { return (CHOC_OBJC_CAST_BRIDGED void*)host; }

    bool confirmReset()
    {
        using namespace choc::objc;
        CHOC_AUTORELEASE_BEGIN
        auto alert = callClass<id>("NSAlert", "new");
        call<void>(alert, "setMessageText:", getNSString("Reset " + appName + "?"));
        call<void>(alert, "setInformativeText:", getNSString("This clears saved website data and signs you out."));
        call<id>(alert, "addButtonWithTitle:", getNSString("Cancel"));
        call<id>(alert, "addButtonWithTitle:", getNSString("Reset"));
        const bool accepted = call<long>(alert, "runModal") == 1001;
        call<void>(alert, "release");
        return accepted;
        CHOC_AUTORELEASE_END
    }

    void showError(const char* message)
    {
        using namespace choc::objc;
        CHOC_AUTORELEASE_BEGIN
        auto alert = callClass<id>("NSAlert", "new");
        call<void>(alert, "setMessageText:", getNSString(message));
        call<id>(alert, "addButtonWithTitle:", getNSString("Close"));
        call<long>(alert, "runModal");
        call<void>(alert, "release");
        CHOC_AUTORELEASE_END
    }

    void setBusy(bool active)
    {
        using namespace choc::objc;
        CHOC_AUTORELEASE_BEGIN
        call<void>(refreshButton, "setEnabled:", static_cast<BOOL>(!active));
        call<void>(resetButton, "setEnabled:", static_cast<BOOL>(!active));
        call<void>(resetButton, "setTitle:", getNSString(active ? "Resetting…" : "Reset"));
        call<void>((CHOC_OBJC_CAST_BRIDGED id)browser.getViewHandle(),
                   "setHidden:", static_cast<BOOL>(active));
        CHOC_AUTORELEASE_END
    }

#elif CHOC_WINDOWS
    HWND host = nullptr;
    HWND ownerWindow = nullptr;
    HWND refreshButton = nullptr;
    HWND resetButton = nullptr;
    HHOOK keyboardHook = nullptr;
    inline static thread_local RecoveryToolbar* keyboardOwner = nullptr;
    static constexpr UINT refreshMessage = WM_APP + 81;

    static LRESULT CALLBACK windowProc(HWND handle, UINT message, WPARAM wParam, LPARAM lParam)
    {
        auto* toolbar = reinterpret_cast<RecoveryToolbar*>(GetWindowLongPtrW(handle, GWLP_USERDATA));
        if (toolbar != nullptr)
        {
            if (message == WM_SIZE)
            {
                toolbar->layout();
                return 0;
            }
            if (message == WM_COMMAND && HIWORD(wParam) == BN_CLICKED)
            {
                if (LOWORD(wParam) == 1) toolbar->refresh();
                if (LOWORD(wParam) == 2) toolbar->reset();
                return 0;
            }
            if (message == refreshMessage)
            {
                toolbar->refresh();
                return 0;
            }
            if (message == WM_SETFOCUS)
                SetFocus(static_cast<HWND>(toolbar->browser.getViewHandle()));
        }
        return DefWindowProcW(handle, message, wParam, lParam);
    }

    static LRESULT CALLBACK keyMessage(int code, WPARAM wParam, LPARAM lParam)
    {
        auto* toolbar = keyboardOwner;
        if (code >= 0 && wParam == PM_REMOVE && toolbar != nullptr && !toolbar->confirming)
        {
            auto* message = reinterpret_cast<MSG*>(lParam);
            if (message->message == WM_KEYDOWN &&
                GetAncestor(message->hwnd, GA_ROOT) == toolbar->ownerWindow &&
                (GetKeyState(VK_MENU) & 0x8000) == 0 &&
                (message->wParam == VK_F5 ||
                 (message->wParam == 'R' && (GetKeyState(VK_CONTROL) & 0x8000) != 0)))
            {
                PostMessageW(toolbar->host, refreshMessage, 0, 0);
                message->message = WM_NULL;
            }
        }
        return CallNextHookEx(nullptr, code, wParam, lParam);
    }

    void initialise(choc::ui::DesktopWindow& window)
    {
        ownerWindow = static_cast<HWND>(window.getWindowHandle());
        static const ATOM windowClass = [] {
            WNDCLASSEXW definition{};
            definition.cbSize = sizeof(definition);
            definition.hInstance = GetModuleHandleW(nullptr);
            definition.lpfnWndProc = windowProc;
            definition.lpszClassName = L"WebAppRecoveryToolbar";
            definition.hCursor = LoadCursorW(nullptr, MAKEINTRESOURCEW(32512));
            definition.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_BTNFACE + 1);
            return RegisterClassExW(&definition);
        }();
        host = CreateWindowExW(0, MAKEINTRESOURCEW(windowClass), L"", WS_POPUP | WS_CLIPCHILDREN,
            0, 0, 640, 480, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
        if (host == nullptr)
            return;
        SetWindowLongPtrW(host, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(this));
        refreshButton = makeButton(L"Refresh", 1);
        resetButton = makeButton(L"Reset", 2);
        auto nativeBrowser = static_cast<HWND>(browser.getViewHandle());
        auto style = GetWindowLongPtrW(nativeBrowser, GWL_STYLE);
        SetWindowLongPtrW(nativeBrowser, GWL_STYLE, (style & ~static_cast<LONG_PTR>(WS_POPUP)) | WS_CHILD);
        SetParent(nativeBrowser, host);
        ShowWindow(nativeBrowser, SW_SHOW);
        layout();
        keyboardOwner = this;
        keyboardHook = SetWindowsHookExW(WH_GETMESSAGE, keyMessage, nullptr, GetCurrentThreadId());
    }

    HWND makeButton(const wchar_t* title, int identifier)
    {
        auto button = CreateWindowExW(0, L"BUTTON", title, WS_CHILD | WS_VISIBLE | WS_TABSTOP,
            0, 0, 90, 28, host, reinterpret_cast<HMENU>(static_cast<INT_PTR>(identifier)),
            GetModuleHandleW(nullptr), nullptr);
        SendMessageW(button, WM_SETFONT, reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
        return button;
    }

    void layout()
    {
        RECT bounds{};
        GetClientRect(host, &bounds);
        auto dpi = GetDpiForWindow(ownerWindow);
        if (dpi == 0) dpi = 96;
        const auto scale = [dpi](int value) { return MulDiv(value, static_cast<int>(dpi), 96); };
        const int barHeight = scale(44);
        MoveWindow(refreshButton, scale(8), scale(8), scale(90), scale(28), TRUE);
        MoveWindow(resetButton, scale(106), scale(8), scale(90), scale(28), TRUE);
        MoveWindow(static_cast<HWND>(browser.getViewHandle()), 0, barHeight,
            static_cast<int>(std::max(0L, bounds.right)),
            static_cast<int>(std::max(0L, bounds.bottom - barHeight)), TRUE);
    }

    void destroy()
    {
        keyboardOwner = nullptr;
        if (keyboardHook != nullptr) UnhookWindowsHookEx(keyboardHook);
        if (IsWindow(host))
        {
            SetWindowLongPtrW(host, GWLP_USERDATA, 0);
            DestroyWindow(host);
        }
    }

    void* nativeViewHandle() const { return host; }

    bool confirmReset()
    {
        const auto title = choc::ui::createUTF16StringFromUTF8("Reset " + appName + "?");
        return MessageBoxW(ownerWindow, L"This clears saved website data and signs you out.",
            title.c_str(), MB_OKCANCEL | MB_ICONQUESTION | MB_DEFBUTTON2) == IDOK;
    }

    void showError(const char* message)
    {
        const auto text = choc::ui::createUTF16StringFromUTF8(message);
        const auto title = choc::ui::createUTF16StringFromUTF8(appName);
        MessageBoxW(ownerWindow, text.c_str(), title.c_str(), MB_OK | MB_ICONERROR);
    }

    void setBusy(bool active)
    {
        EnableWindow(refreshButton, !active);
        EnableWindow(resetButton, !active);
        SetWindowTextW(resetButton, active ? L"Resetting…" : L"Reset");
        EnableWindow(static_cast<HWND>(browser.getViewHandle()), !active);
    }
#endif
};
} // namespace webapp::ui
