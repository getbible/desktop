// Exercise the real native recovery controls against a local HTTP fixture.
#include <gui/choc_MessageLoop.h>
#include <gui/choc_DesktopWindow.h>
#include <gui/choc_WebView.h>
#include "recovery_toolbar.hpp"
#include "app_config.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <string>

namespace
{
GtkWidget* findButton(GtkWidget* widget, const char* label)
{
    if (GTK_IS_BUTTON(widget) && g_strcmp0(gtk_button_get_label(GTK_BUTTON(widget)), label) == 0)
        return widget;
    if (!GTK_IS_CONTAINER(widget))
        return nullptr;
    auto* children = gtk_container_get_children(GTK_CONTAINER(widget));
    GtkWidget* found = nullptr;
    for (auto* item = children; item != nullptr && found == nullptr; item = item->next)
        found = findButton(GTK_WIDGET(item->data), label);
    g_list_free(children);
    return found;
}

class RecoverySmoke
{
public:
    RecoverySmoke(choc::ui::WebView& view, webapp::ui::RecoveryToolbar& controls,
                  choc::ui::DesktopWindow& desktop, bool onlyVerifyEmpty, bool testLegacyScope)
        : browser(view), toolbar(controls), window(desktop), verifyEmpty(onlyVerifyEmpty),
          legacyScope(testLegacyScope)
    {
        refreshButton = findButton(GTK_WIDGET(toolbar.getViewHandle()), "Refresh");
        resetButton = findButton(GTK_WIDGET(toolbar.getViewHandle()), "Reset");
        auto* nativeView = WEBKIT_WEB_VIEW(browser.getViewHandle());
        g_signal_connect(nativeView, "load-changed", G_CALLBACK(+[](WebKitWebView* view,
            WebKitLoadEvent event, gpointer data) {
            if (event == WEBKIT_LOAD_FINISHED)
                static_cast<RecoverySmoke*>(data)->loaded(view);
        }), this);
        g_signal_connect(nativeView, "load-failed", G_CALLBACK(+[](WebKitWebView*, WebKitLoadEvent,
            const char*, GError* error, gpointer data) -> gboolean {
            static_cast<RecoverySmoke*>(data)->fail(error != nullptr ? error->message : "Page failed to load");
            return TRUE;
        }), this);
    }

    bool passed = false;

    void fail(const std::string& message)
    {
        std::fprintf(stderr, "Recovery smoke test: %s\n", message.c_str());
        passed = false;
        gtk_main_quit();
    }

private:
    enum class Stage { initial, seedUnrelated, returnFromSeed, refreshing,
                       afterFailedReset, resetting, verifyUnrelated, finished } stage = Stage::initial;
    choc::ui::WebView& browser;
    webapp::ui::RecoveryToolbar& toolbar;
    choc::ui::DesktopWindow& window;
    GtkWidget* refreshButton = nullptr;
    GtkWidget* resetButton = nullptr;
    bool verifyEmpty = false;
    bool legacyScope = false;
    bool acceptDialog = false;
    bool sawDialog = false;

    std::string unrelatedURL() const
    {
        auto url = std::string(webapp::config::appURL);
        const auto position = url.find("127.0.0.1");
        url.replace(position, std::strlen("127.0.0.1"), "localhost");
        return url;
    }

    void evaluate(const std::string& script, std::function<void()> next)
    {
        if (!browser.evaluateJavascript(script, [this, next = std::move(next)](
            const std::string& error, const choc::value::ValueView& result) {
            if (!error.empty() || !result.isBool() || !result.getBool())
            {
                fail(error.empty() ? "Page state did not match the expected operation" : error);
                return;
            }
            next();
        }))
            fail("Unable to evaluate fixture state");
    }

    void captureScreenshot()
    {
        const auto* path = std::getenv("WEBAPP_RECOVERY_SCREENSHOT");
        if (path == nullptr || *path == '\0')
            return;
        auto* widget = GTK_WIDGET(window.getWindowHandle());
        auto* drawable = gtk_widget_get_window(widget);
        if (drawable == nullptr)
            return;
        auto* pixels = gdk_pixbuf_get_from_window(drawable, 0, 0,
            gtk_widget_get_allocated_width(widget), gtk_widget_get_allocated_height(widget));
        if (pixels != nullptr)
        {
            GError* error = nullptr;
            gdk_pixbuf_save(pixels, path, "png", &error, nullptr);
            if (error != nullptr)
                g_error_free(error);
            g_object_unref(pixels);
        }
    }

    void loaded(WebKitWebView* view)
    {
        if (g_strcmp0(webkit_web_view_get_uri(view), unrelatedURL().c_str()) == 0)
        {
            if (stage == Stage::seedUnrelated)
            {
                stage = Stage::returnFromSeed;
                evaluate(R"JS((() => {
                    localStorage.setItem('unrelated-site', 'keep-this-value');
                    document.cookie = 'unrelated-site=keep-this-value; Max-Age=3600; Path=/';
                    return localStorage.getItem('unrelated-site') === 'keep-this-value' &&
                        document.cookie.includes('unrelated-site=keep-this-value');
                })())JS", [this] { browser.navigate(std::string(webapp::config::appURL)); });
            }
            else if (stage == Stage::verifyUnrelated)
            {
                stage = Stage::finished;
                evaluate(R"JS(localStorage.getItem('unrelated-site') === 'keep-this-value' &&
                    document.cookie.includes('unrelated-site=keep-this-value'))JS", [this] {
                    finish("Legacy shared storage kept the unrelated website's cookies and localStorage.");
                });
            }
            return;
        }
        // Reset first unloads to about:blank; only inspect the reloaded fixture.
        if (g_strcmp0(webkit_web_view_get_uri(view), std::string(webapp::config::appURL).c_str()) != 0)
            return;
        if (stage == Stage::initial && verifyEmpty)
        {
            stage = Stage::finished;
            evaluate(R"JS(localStorage.getItem('recovery-smoke') === null &&
                !document.cookie.split(';').some(v => v.trim().startsWith('recovery-smoke=')))JS",
                [this] { finish("Confirmed reset remains cleared after restarting the browser."); });
            return;
        }
        if (stage == Stage::initial && legacyScope)
        {
            stage = Stage::seedUnrelated;
            browser.navigate(unrelatedURL());
            return;
        }
        if (stage == Stage::initial || stage == Stage::returnFromSeed)
        {
            if (refreshButton == nullptr || resetButton == nullptr ||
                !gtk_widget_get_mapped(refreshButton) || !gtk_widget_get_mapped(resetButton))
                return fail("Native Refresh and Reset buttons are not visible");
            GtkAllocation refreshBounds {}, browserBounds {};
            gtk_widget_get_allocation(refreshButton, &refreshBounds);
            gtk_widget_get_allocation(GTK_WIDGET(browser.getViewHandle()), &browserBounds);
            if (browserBounds.y <= refreshBounds.y)
                return fail("Native buttons are not placed above the website");
            stage = Stage::refreshing;
            evaluate(R"JS((() => {
                localStorage.setItem('recovery-smoke', 'saved-login-state');
                localStorage.setItem('recovery-generation', document.body.dataset.generation);
                document.cookie = 'recovery-smoke=saved-login-state; Max-Age=3600; Path=/; SameSite=Lax';
                return localStorage.getItem('recovery-smoke') === 'saved-login-state' &&
                    document.cookie.includes('recovery-smoke=saved-login-state');
            })())JS", [this] {
                captureScreenshot();
                gtk_button_clicked(GTK_BUTTON(refreshButton));
            });
            return;
        }
        if (stage == Stage::refreshing)
        {
            // A fresh generation proves Refresh bypassed the cacheable HTML.
            evaluate(R"JS(localStorage.getItem('recovery-smoke') === 'saved-login-state' &&
                document.cookie.includes('recovery-smoke=saved-login-state') &&
                Number(document.body.dataset.generation) > Number(localStorage.getItem('recovery-generation')) &&
                (window.unsavedDraft = 'retain this draft when Reset is cancelled', true))JS", [this] {
                stage = Stage::resetting;
                clickReset(false);
                evaluate(R"JS(window.unsavedDraft === 'retain this draft when Reset is cancelled' &&
                    localStorage.getItem('recovery-smoke') === 'saved-login-state' &&
                    document.cookie.includes('recovery-smoke=saved-login-state'))JS", [this] {
                    std::puts("Refresh bypassed HTTP cache and retained login data; cancelling Reset retained the page and login data.");
                    // Fail the unload before its asynchronous finish event.
                    // Reset must recover its native controls and keep login data.
                    clickReset(true);
                    stage = Stage::afterFailedReset;
                    g_idle_add(+[](gpointer) -> gboolean {
                        auto* windows = gtk_window_list_toplevels();
                        for (auto* item = windows; item != nullptr; item = item->next)
                        {
                            if (GTK_IS_DIALOG(item->data) &&
                                gtk_dialog_get_widget_for_response(GTK_DIALOG(item->data), GTK_RESPONSE_CLOSE) != nullptr)
                                gtk_dialog_response(GTK_DIALOG(item->data), GTK_RESPONSE_CLOSE);
                        }
                        g_list_free(windows);
                        return G_SOURCE_REMOVE;
                    }, nullptr);
                    auto* error = g_error_new_literal(WEBKIT_NETWORK_ERROR, WEBKIT_NETWORK_ERROR_FAILED,
                                                     "Fixture unload failure");
                    gboolean handled = FALSE;
                    g_signal_emit_by_name(browser.getViewHandle(), "load-failed", WEBKIT_LOAD_STARTED,
                                          "about:blank", error, &handled);
                    g_error_free(error);
                    if (!gtk_widget_get_sensitive(refreshButton) || !gtk_widget_get_sensitive(resetButton))
                        return fail("Recovery controls stayed disabled after a failed reset");
                    gtk_button_clicked(GTK_BUTTON(refreshButton));
                });
            });
            return;
        }
        if (stage == Stage::afterFailedReset)
        {
            evaluate(R"JS(localStorage.getItem('recovery-smoke') === 'saved-login-state' &&
                document.cookie.includes('recovery-smoke=saved-login-state'))JS", [this] {
                std::puts("A failed Reset restored controls and retained login data; Refresh recovered the page.");
                stage = Stage::resetting;
                clickReset(true);
            });
            return;
        }
        if (stage == Stage::resetting)
        {
            stage = Stage::finished;
            evaluate(R"JS(localStorage.getItem('recovery-smoke') === null &&
                !document.cookie.split(';').some(v => v.trim().startsWith('recovery-smoke=')))JS", [this] {
                if (!gtk_widget_get_sensitive(refreshButton) || !gtk_widget_get_sensitive(resetButton))
                    return fail("Recovery controls did not become available after reset");
                std::puts("Confirmed Reset removed cookies and localStorage before reopening the website.");
                if (legacyScope)
                {
                    stage = Stage::verifyUnrelated;
                    browser.navigate(unrelatedURL());
                }
                else
                    finish("Native recovery controls completed successfully.");
            });
        }
    }

    void clickReset(bool accept)
    {
        acceptDialog = accept;
        sawDialog = false;
        g_idle_add(+[](gpointer data) -> gboolean {
            auto* test = static_cast<RecoverySmoke*>(data);
            auto* windows = gtk_window_list_toplevels();
            for (auto* item = windows; item != nullptr; item = item->next)
            {
                if (!GTK_IS_DIALOG(item->data))
                    continue;
                auto* dialog = GTK_DIALOG(item->data);
                auto* cancel = gtk_dialog_get_widget_for_response(dialog, GTK_RESPONSE_CANCEL);
                auto* reset = gtk_dialog_get_widget_for_response(dialog, GTK_RESPONSE_ACCEPT);
                if (cancel == nullptr || reset == nullptr)
                    continue;
                if (gtk_window_get_default_widget(GTK_WINDOW(dialog)) != cancel)
                {
                    test->fail("Reset confirmation does not default to Cancel");
                    gtk_dialog_response(dialog, GTK_RESPONSE_CANCEL);
                    break;
                }
                test->sawDialog = true;
                gtk_dialog_response(dialog, test->acceptDialog ? GTK_RESPONSE_ACCEPT : GTK_RESPONSE_CANCEL);
                break;
            }
            g_list_free(windows);
            return G_SOURCE_REMOVE;
        }, this);
        gtk_button_clicked(GTK_BUTTON(resetButton));
        if (!sawDialog)
            fail("Reset did not present its native confirmation dialog");
    }

    void finish(const char* message)
    {
        std::puts(message);
        passed = true;
        gtk_main_quit();
    }
};
} // namespace

int main(int argc, char** argv)
{
    if (argc != 2 || (std::strcmp(argv[1], "exercise") != 0 && std::strcmp(argv[1], "verify-empty") != 0))
        return 2;
    gtk_init(&argc, &argv);
    choc::messageloop::initialise();
    choc::ui::WebView browser;
    if (!browser.loadedOK())
        return 1;
    choc::ui::DesktopWindow window({0, 0, 800, 550});
    window.setWindowTitle("Desktop recovery controls");
    webapp::ui::RecoveryToolbar toolbar(browser, window, std::string(webapp::config::appURL), "Recovery fixture");
    window.setContent(toolbar.getViewHandle());
    window.setVisible(true);
    RecoverySmoke test(browser, toolbar, window, std::strcmp(argv[1], "verify-empty") == 0,
                       webapp::config::appID == "live.getbible.app");
    const auto watchdog = g_timeout_add_seconds(30, +[](gpointer data) -> gboolean {
        static_cast<RecoverySmoke*>(data)->fail("Timed out waiting for native recovery controls");
        return G_SOURCE_REMOVE;
    }, &test);
    browser.navigate(std::string(webapp::config::appURL));
    gtk_main();
    if (g_main_context_find_source_by_id(nullptr, watchdog) != nullptr)
        g_source_remove(watchdog);
    return test.passed ? 0 : 1;
}
