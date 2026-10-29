// A local-origin integration test for the same context factory as the app.
#include "webview_storage.hpp"
#include <cstdio>
#include <cstring>

struct TestState
{
    const char* operation = nullptr;
    bool evaluated = false;
    bool passed = false;
};

static void fail(const char* message)
{
    std::fprintf(stderr, "Storage smoke test: %s\n", message);
    gtk_main_quit();
}

int main(int argc, char** argv)
{
    if (argc != 5 ||
        (std::strcmp(argv[2], "true") != 0 && std::strcmp(argv[2], "false") != 0) ||
        (std::strcmp(argv[3], "write") != 0 && std::strcmp(argv[3], "read") != 0 &&
         std::strcmp(argv[3], "empty") != 0))
    {
        std::fprintf(stderr, "Usage: storage-smoke APP_ID true|false write|read|empty URL\n");
        return 2;
    }

    // The fixture's generated header uses mutable equivalents of the app's
    // compile-time fields, so one executable can test different profiles.
    webapp::config::appID = argv[1];
    webapp::config::persistent = std::strcmp(argv[2], "true") == 0;
    gtk_init(&argc, &argv);
    auto* context = webapp::storage::createLinuxContext();
    if (context == nullptr)
        return 1;
    auto* manager = webkit_web_context_get_website_data_manager(context);
    if (static_cast<bool>(webkit_website_data_manager_is_ephemeral(manager)) == webapp::config::persistent)
    {
        std::fputs("Context persistence does not match the requested profile.\n", stderr);
        g_object_unref(context);
        return 1;
    }

    TestState state {argv[3], false, false};
    auto* window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    auto* view = WEBKIT_WEB_VIEW(webkit_web_view_new_with_context(context));
    gtk_container_add(GTK_CONTAINER(window), GTK_WIDGET(view));
    g_signal_connect(view, "close", G_CALLBACK(+[](WebKitWebView*, gpointer) { gtk_main_quit(); }), nullptr);
    g_signal_connect(view, "load-failed", G_CALLBACK(+[](WebKitWebView*, WebKitLoadEvent,
        const char*, GError* error, gpointer) -> gboolean {
        fail(error != nullptr ? error->message : "page load failed");
        return TRUE;
    }), nullptr);
    g_signal_connect(view, "load-changed", G_CALLBACK(+[](WebKitWebView* webView,
        WebKitLoadEvent event, gpointer data) {
        auto* test = static_cast<TestState*>(data);
        if (event != WEBKIT_LOAD_FINISHED || test->evaluated)
            return;
        test->evaluated = true;
        const char* script = nullptr;
        if (std::strcmp(test->operation, "write") == 0)
            script = R"JS((() => {
                localStorage.setItem('desktop-storage-smoke', 'stored-value');
                document.cookie = 'desktop-storage-smoke=stored-value; Max-Age=3600; Path=/; SameSite=Lax';
                return localStorage.getItem('desktop-storage-smoke') === 'stored-value'
                    && document.cookie.split(';').some(v => v.trim() === 'desktop-storage-smoke=stored-value');
            })())JS";
        else if (std::strcmp(test->operation, "read") == 0)
            script = R"JS(localStorage.getItem('desktop-storage-smoke') === 'stored-value'
                && document.cookie.split(';').some(v => v.trim() === 'desktop-storage-smoke=stored-value'))JS";
        else
            script = R"JS(localStorage.getItem('desktop-storage-smoke') === null
                && !document.cookie.split(';').some(v => v.trim().startsWith('desktop-storage-smoke=')))JS";

        webkit_web_view_evaluate_javascript(webView, script, -1, nullptr, nullptr, nullptr,
            +[](GObject* object, GAsyncResult* result, gpointer callbackData) {
                auto* current = static_cast<TestState*>(callbackData);
                GError* error = nullptr;
                auto* value = webkit_web_view_evaluate_javascript_finish(WEBKIT_WEB_VIEW(object), result, &error);
                if (error != nullptr)
                {
                    fail(error->message);
                    g_error_free(error);
                    return;
                }
                current->passed = value != nullptr && jsc_value_is_boolean(value) && jsc_value_to_boolean(value);
                if (value != nullptr)
                    g_object_unref(value);
                if (!current->passed)
                {
                    fail("cookie/localStorage state did not match the expected operation");
                    return;
                }
                // Ask the web process to close the document normally before
                // releasing the view; do not kill its storage/network process.
                webkit_web_view_try_close(WEBKIT_WEB_VIEW(object));
            }, test);
    }), &state);

    const auto watchdog = g_timeout_add_seconds(20, +[](gpointer data) -> gboolean {
        static_cast<TestState*>(data)->passed = false;
        fail("timed out waiting for WebKit");
        return G_SOURCE_REMOVE;
    }, &state);
    gtk_widget_show_all(window);
    webkit_web_view_load_uri(view, argv[4]);
    gtk_main();
    if (g_main_context_find_source_by_id(nullptr, watchdog) != nullptr)
        g_source_remove(watchdog);
    gtk_widget_destroy(window);
    g_object_unref(context);
    return state.passed ? 0 : 1;
}
