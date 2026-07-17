void configureWebView(choc::ui::WebView& webView)
{
    webView.addInitScript(createApplicationIdentityScript());
    webView.addInitScript(createNavigationPolicyScript());

    const auto menuScript = createAppMenuScript();
    if (!menuScript.empty())
    {
        webView.addInitScript(menuScript);
    }
}

#if CHOC_LINUX
void configureLinuxApplicationIdentity(choc::ui::WebView& webView)
{
    auto* nativeView = static_cast<GtkWidget*>(webView.getViewHandle());
    if (nativeView == nullptr || !WEBKIT_IS_WEB_VIEW(nativeView)) return;

    WebKitSettings* settings = webkit_web_view_get_settings(WEBKIT_WEB_VIEW(nativeView));
    if (settings == nullptr) return;

    const char* currentUserAgent = webkit_settings_get_user_agent(settings);
    std::string userAgent = currentUserAgent != nullptr ? currentUserAgent : "";
    const std::string productPrefix = std::string(userAgentProduct) + "/";

    if (userAgent.find(productPrefix) != std::string::npos) return;
    if (!userAgent.empty()) userAgent.push_back(' ');

    userAgent += productPrefix;
    userAgent += version;
    webkit_settings_set_user_agent(settings, userAgent.c_str());
}

bool isAllowedLinuxNavigation(const char* uri)
{
    if (uri == nullptr || *uri == '\0') return false;

    const std::string_view value(uri);
    if (value == "about:blank") return true;

    if (value.rfind("blob:", 0) == 0)
    {
        return isAllowedLinuxNavigation(uri + 5);
    }

    GError* error = nullptr;
    GUri* parsedURI = g_uri_parse(uri, G_URI_FLAGS_PARSE_RELAXED, &error);
    if (parsedURI == nullptr)
    {
        g_clear_error(&error);
        return false;
    }

    const char* scheme = g_uri_get_scheme(parsedURI);
    const char* host = g_uri_get_host(parsedURI);
    const bool shellPage =
        scheme != nullptr && host != nullptr &&
        g_ascii_strcasecmp(scheme, "webapp") == 0 &&
        g_ascii_strcasecmp(host, "app") == 0;

    bool allowedWebsite = false;
    if (scheme != nullptr && host != nullptr && g_ascii_strcasecmp(scheme, "https") == 0)
    {
        const auto origin = getLinuxOrigin(uri);
        allowedWebsite = origin.has_value() && hasAllowedOrigin(*origin);
    }

    g_uri_unref(parsedURI);
    return shellPage || allowedWebsite;
}

void configureLinuxNavigationPolicy(choc::ui::WebView& webView)
{
    auto* nativeView = static_cast<GtkWidget*>(webView.getViewHandle());
    if (nativeView == nullptr || !WEBKIT_IS_WEB_VIEW(nativeView)) return;

    gtk_widget_add_events(nativeView, GDK_BUTTON_PRESS_MASK);

    (void)g_signal_connect(
        nativeView,
        "decide-policy",
        G_CALLBACK(+[](WebKitWebView*, WebKitPolicyDecision* decision,
                       WebKitPolicyDecisionType decisionType, gpointer userData) -> gboolean {
            WebKitURIRequest* request = nullptr;
            const bool newWindow = decisionType == WEBKIT_POLICY_DECISION_TYPE_NEW_WINDOW_ACTION;

            if (decisionType == WEBKIT_POLICY_DECISION_TYPE_NAVIGATION_ACTION || newWindow)
            {
                auto* navigationDecision = WEBKIT_NAVIGATION_POLICY_DECISION(decision);
                WebKitNavigationAction* action =
                    webkit_navigation_policy_decision_get_navigation_action(navigationDecision);
                request = action != nullptr ? webkit_navigation_action_get_request(action) : nullptr;
            }
            else if (decisionType == WEBKIT_POLICY_DECISION_TYPE_RESPONSE)
            {
                auto* responseDecision = WEBKIT_RESPONSE_POLICY_DECISION(decision);
                request = webkit_response_policy_decision_get_request(responseDecision);
            }
            else
            {
                return FALSE;
            }

            const char* uri = request != nullptr ? webkit_uri_request_get_uri(request) : nullptr;
            const bool allowed = isAllowedLinuxNavigation(uri);

            if (allowed && !newWindow) return FALSE;

            webkit_policy_decision_ignore(decision);
            auto* applicationWebView = static_cast<choc::ui::WebView*>(userData);
            if (applicationWebView != nullptr)
            {
                applicationWebView->navigate(
                    allowed && uri != nullptr ? std::string(uri) : std::string(appURL));
            }
            return TRUE;
        }),
        &webView);

    (void)g_signal_connect(
        nativeView,
        "context-menu",
        G_CALLBACK(+[](WebKitWebView*, WebKitContextMenu*, GdkEvent*, WebKitHitTestResult*, gpointer) -> gboolean {
            return TRUE;
        }),
        nullptr);

    (void)g_signal_connect(
        nativeView,
        "button-press-event",
        G_CALLBACK(+[](GtkWidget*, GdkEventButton* event, gpointer) -> gboolean {
            return event != nullptr && event->button != 1 ? TRUE : FALSE;
        }),
        nullptr);
}
#endif

int runApplication()
{
    choc::ui::setWindowsDPIAwareness();

    choc::ui::WebView::Options options;
    options.customSchemeURI = std::string(shellURL);
    options.fetchResource = fetchEmbeddedResource;
    options.transparentBackground = false;

    bool debugEnabled = std::getenv("WEBAPP_DEBUG") != nullptr;
#if defined(WEBAPP_DEBUG_DEFAULT)
    debugEnabled = true;
#endif

    options.enableDebugMode = debugEnabled;
    options.enableDebugInspector = debugEnabled;
    options.webviewIsReady = [](choc::ui::WebView& webView) {
        configureWebView(webView);
#if CHOC_LINUX
        configureLinuxApplicationIdentity(webView);
        configureLinuxNavigationPolicy(webView);
#endif
    };

    choc::ui::WebView webView(options);
    if (!webView.loadedOK())
    {
        std::cerr << name << " could not initialise the system web view." << std::endl;
        return EXIT_FAILURE;
    }

    choc::ui::DesktopWindow window(choc::ui::Bounds{
        0,
        0,
        webapp::config::windowWidth,
        webapp::config::windowHeight});
    window.setVisible(false);
    window.setResizable(true);
    window.setMinimumSize(
        webapp::config::minimumWindowWidth,
        webapp::config::minimumWindowHeight);
    window.centreWithSize(
        webapp::config::windowWidth,
        webapp::config::windowHeight);
    window.setWindowTitle(std::string(name));
    window.setContent(webView.getViewHandle());
    window.windowClosed = []() { choc::messageloop::stop(); };
    window.setVisible(true);
    window.toFront();

    choc::messageloop::run();
    return EXIT_SUCCESS;
}
} // namespace

#if defined(_WIN32)
int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
    return runApplication();
}
#else
int main()
{
    return runApplication();
}
#endif
