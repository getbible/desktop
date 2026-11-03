// Exercise the application's actual navigation handlers in native WebKitGTK.
// The custom resource fixture stays entirely inside this process: no accounts,
// TLS exceptions, or internet services are needed.
#define main desktop_application_main
#include "../src/main.cpp"
#undef main

#include <cstring>

namespace
{
struct NavigationTest
{
    choc::ui::WebView* view = nullptr;
    int phase = 0;
    bool pending = false;
    bool passed = false;
    bool unexpectedNavigation = false;
    gint64 nextCheck = 0;

    void fail(const std::string& message)
    {
        std::cerr << "Navigation smoke test: " << message << '\n';
        phase = -1;
        gtk_main_quit();
    }

    void evaluate(const char* script, int nextPhase)
    {
        pending = true;
        if (!view->evaluateJavascript(script,
            [this, nextPhase](const std::string& error, const choc::value::ValueView& result) {
                pending = false;
                if (!error.empty() || !result.isBool() || !result.getBool())
                {
                    fail(error.empty() ? "document state or navigation result did not match" : error);
                    return;
                }
                phase = nextPhase;
                nextCheck = g_get_monotonic_time() + 200000;
            }))
        {
            pending = false;
            fail("could not evaluate the fixture");
        }
    }

    gboolean tick()
    {
        if (phase < 0) return G_SOURCE_REMOVE;
        if (pending || view == nullptr || !view->isReady() || g_get_monotonic_time() < nextCheck)
            return G_SOURCE_CONTINUE;
        if (unexpectedNavigation)
        {
            fail("a rejected request navigated the whole application away from the fixture");
            return G_SOURCE_REMOVE;
        }
        switch (phase)
        {
            case 0:
                // Wait for the initial custom-scheme document without an
                // arbitrary sleep before evaluating its content.
                if (webkit_web_view_is_loading(WEBKIT_WEB_VIEW(view->getViewHandle()))) break;
                evaluate(R"JS((() => {
                    const input = document.querySelector('#unsaved');
                    if (!input) return false;
                    input.value = 'unsaved draft';
                    window.navigationSmokeSentinel = 'original document';
                    window.dispatchEvent(new Event('offline'));
                    window.dispatchEvent(new Event('online'));
                    const anchor = document.querySelector('#external');
                    const click = new MouseEvent('click', { bubbles: true, cancelable: true, button: 0 });
                    anchor.dispatchEvent(click);
                    const form = document.querySelector('#external-form');
                    const submit = new Event('submit', { bubbles: true, cancelable: true });
                    form.dispatchEvent(submit);
                    return click.defaultPrevented && submit.defaultPrevented
                        && window.open('https://blocked.invalid/popup') === null;
                })())JS", 1);
                break;
            case 1:
                evaluate(R"JS((() => {
                    const frame = document.createElement('iframe');
                    frame.src = 'https://blocked.invalid/frame';
                    document.body.appendChild(frame);
                    // This bypasses the injected click handlers and exercises
                    // the native navigation guard for the main document.
                    window.location.assign('https://blocked.invalid/top-level');
                    return true;
                })())JS", 2);
                break;
            case 2:
                evaluate(R"JS(window.navigationSmokeSentinel === 'original document'
                    && document.querySelector('#unsaved')?.value === 'unsaved draft'
                    && window.location.href === 'webapp://app/')JS", 3);
                break;
            case 3:
                // Same-origin popup requests must still work in the existing
                // app window, rather than being blocked with external links.
                evaluate(R"JS((() => {
                    window.open('webapp://app/next', '_blank');
                    return true;
                })())JS", 4);
                break;
            case 4:
                if (webkit_web_view_is_loading(WEBKIT_WEB_VIEW(view->getViewHandle()))) break;
                evaluate(R"JS(window.location.href === 'webapp://app/next'
                    && document.querySelector('#destination') !== null
                    && window.navigationSmokeSentinel === undefined)JS", 5);
                break;
            case 5:
                passed = true;
                gtk_main_quit();
                return G_SOURCE_REMOVE;
            default:
                fail("unexpected test phase");
                return G_SOURCE_REMOVE;
        }
        return G_SOURCE_CONTINUE;
    }
};
}

int main()
{
    NavigationTest test;
    choc::ui::WebView::Options options;
    options.customSchemeURI = "webapp://app";
    options.fetchResource = [](const std::string& path)
        -> std::optional<choc::ui::WebView::Options::Resource> {
        const std::string html = path == "/next"
            ? "<!doctype html><html><body><p id='destination'>Allowed destination</p></body></html>"
            : "<!doctype html><html><body><input id='unsaved'>"
              "<a id='external' href='https://blocked.invalid/link'>External link</a>"
              "<form id='external-form' action='https://blocked.invalid/form'></form>"
              "</body></html>";
        return choc::ui::WebView::Options::Resource(html, "text/html; charset=utf-8");
    };
    options.webviewIsReady = [&test](choc::ui::WebView& view) {
        test.view = &view;
        view.addInitScript(createNavigationPolicyScript());
        configureLinuxNavigationPolicy(view);
        // Even under a regression, prevent a navigation to the real configured
        // website. The production handler runs first and normally cancels
        // disallowed destinations before this safety handler sees them.
        g_signal_connect(view.getViewHandle(), "decide-policy",
            G_CALLBACK(+[](WebKitWebView*, WebKitPolicyDecision* decision,
                WebKitPolicyDecisionType type, gpointer data) -> gboolean {
                if (type != WEBKIT_POLICY_DECISION_TYPE_NAVIGATION_ACTION
                    && type != WEBKIT_POLICY_DECISION_TYPE_NEW_WINDOW_ACTION) return FALSE;
                auto* action = webkit_navigation_policy_decision_get_navigation_action(
                    WEBKIT_NAVIGATION_POLICY_DECISION(decision));
                auto* request = action != nullptr ? webkit_navigation_action_get_request(action) : nullptr;
                const char* uri = request != nullptr ? webkit_uri_request_get_uri(request) : nullptr;
                if (uri != nullptr && std::strncmp(uri, "webapp://app", 12) == 0) return FALSE;
                auto* current = static_cast<NavigationTest*>(data);
                current->unexpectedNavigation = true;
                webkit_policy_decision_ignore(decision);
                return TRUE;
            }), &test);
    };
    choc::ui::WebView view(options);
    if (!view.loadedOK() || test.view == nullptr)
    {
        std::cerr << "Navigation smoke test: native WebView could not initialise\n";
        return 1;
    }
    auto* window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
    gtk_container_add(GTK_CONTAINER(window), GTK_WIDGET(view.getViewHandle()));
    gtk_widget_show_all(window);
    const auto timer = g_timeout_add(50, +[](gpointer data) -> gboolean {
        return static_cast<NavigationTest*>(data)->tick();
    }, &test);
    const auto watchdog = g_timeout_add_seconds(20, +[](gpointer data) -> gboolean {
        static_cast<NavigationTest*>(data)->fail("timed out");
        return G_SOURCE_REMOVE;
    }, &test);
    gtk_main();
    if (!test.passed) g_source_remove(timer);
    if (test.phase != -1 || test.passed) g_source_remove(watchdog);
    gtk_widget_destroy(window);
    if (!test.passed) return 1;
    std::cout << "Native external-link/frame confinement, allowed navigation, and offline document preservation passed.\n";
    return 0;
}
