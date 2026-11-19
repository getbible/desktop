// Included inside CHOC's Linux WebView::Pimpl by the checked header overlay.
bool browsingDataResetInProgress = false;
std::uint64_t recoveryGeneration = 0;
gulong recoveryNavigationHandler = 0, recoveryFailureHandler = 0, recoveryProcessHandler = 0;
GCancellable* recoveryCancellable = nullptr;
std::function<void(bool)> recoveryCompletion;

bool reload(bool ignoreCache)
{
    if (browsingDataResetInProgress || webview == nullptr)
        return false;
    const char* uri = webkit_web_view_get_uri(WEBKIT_WEB_VIEW(webview));
    if (webapp::recovery::refreshNeedsHome(uri != nullptr ? uri : ""))
        return navigate(std::string(webapp::config::appURL));
    if (ignoreCache)
        webkit_web_view_reload_bypass_cache(WEBKIT_WEB_VIEW(webview));
    else
        webkit_web_view_reload(WEBKIT_WEB_VIEW(webview));
    return true;
}

void detachRecoverySignals()
{
    for (auto* handler : {&recoveryNavigationHandler, &recoveryFailureHandler, &recoveryProcessHandler})
    {
        if (*handler != 0 && webview != nullptr)
            g_signal_handler_disconnect(webview, *handler);
        *handler = 0;
    }
}

void disconnectRecoveryNavigation()
{
    detachRecoverySignals();
    if (recoveryCancellable != nullptr)
        g_cancellable_cancel(recoveryCancellable);
    g_clear_object(&recoveryCancellable);
}

void finishRecovery(bool success)
{
    disconnectRecoveryNavigation();
    browsingDataResetInProgress = false;
    auto callback = std::move(recoveryCompletion);
    recoveryCompletion = {};
    if (callback) callback(success);
}

struct RecoveryDataRequest
{
    Pimpl* owner;
    std::shared_ptr<DeletionChecker> lifetime;
    std::uint64_t generation;
    bool active() const
    {
        return !lifetime->deleted && owner->browsingDataResetInProgress && owner->recoveryGeneration == generation;
    }
};

// Only the original GetBible profile keeps legacy shared WebKit directories.
// WebKit groups website records by registrable domain (eTLD+1), so GetBible
// sibling subdomains may share a record. Preserve records for other domains.
bool recoveryRecordMatchesAllowedHost(WebKitWebsiteData* record) const
{
    const char* name = webkit_website_data_get_name(record);
    if (name == nullptr) return false;
    while (*name == '.') ++name;
    for (auto origin : webapp::config::allowedOrigins)
    {
        GUri* uri = g_uri_parse(std::string(origin).c_str(), G_URI_FLAGS_NONE, nullptr);
        const char* host = uri != nullptr ? g_uri_get_host(uri) : nullptr;
        GError* error = nullptr;
        const char* domain = host != nullptr ? soup_tld_get_base_domain(host, &error) : nullptr;
        // IP addresses and localhost have no registrable domain.
        if (domain == nullptr) domain = host;
        const bool matches = domain != nullptr && g_ascii_strcasecmp(name, domain) == 0;
        g_clear_error(&error);
        if (uri != nullptr) g_uri_unref(uri);
        if (matches) return true;
    }
    return false;
}

static void recoveryDataRemoved(GObject* object, GAsyncResult* result, gpointer data)
{
    std::unique_ptr<RecoveryDataRequest> pending(static_cast<RecoveryDataRequest*>(data));
    GError* error = nullptr;
    const bool success = std::string_view(webapp::config::appID) == "live.getbible.app"
        ? webkit_website_data_manager_remove_finish(WEBKIT_WEBSITE_DATA_MANAGER(object), result, &error)
        : webkit_website_data_manager_clear_finish(WEBKIT_WEBSITE_DATA_MANAGER(object), result, &error);
    g_clear_error(&error);
    if (pending->active()) pending->owner->finishRecovery(success);
}

void clearRecoveryData()
{
    auto* managerToClear = webkit_web_context_get_website_data_manager(webviewContext);
    auto* request = new RecoveryDataRequest{this, deletionChecker, recoveryGeneration};
    if (std::string_view(webapp::config::appID) != "live.getbible.app")
    {
        webkit_website_data_manager_clear(managerToClear, WEBKIT_WEBSITE_DATA_ALL, 0,
                                         recoveryCancellable, recoveryDataRemoved, request);
        return;
    }
    webkit_website_data_manager_fetch(
        managerToClear, WEBKIT_WEBSITE_DATA_ALL, recoveryCancellable,
        +[](GObject* object, GAsyncResult* result, gpointer data) {
            std::unique_ptr<RecoveryDataRequest> pending(static_cast<RecoveryDataRequest*>(data));
            GError* error = nullptr;
            GList* records = webkit_website_data_manager_fetch_finish(WEBKIT_WEBSITE_DATA_MANAGER(object), result, &error);
            if (error != nullptr || !pending->active())
            {
                g_clear_error(&error);
                g_list_free_full(records, reinterpret_cast<GDestroyNotify>(webkit_website_data_unref));
                if (pending->active()) pending->owner->finishRecovery(false);
                return;
            }
            GList* selected = nullptr;
            for (GList* item = records; item != nullptr; item = item->next)
            {
                auto* record = static_cast<WebKitWebsiteData*>(item->data);
                if (pending->owner->recoveryRecordMatchesAllowedHost(record))
                    selected = g_list_prepend(selected, webkit_website_data_ref(record));
            }
            g_list_free_full(records, reinterpret_cast<GDestroyNotify>(webkit_website_data_unref));
            if (selected == nullptr)
            {
                pending->owner->finishRecovery(true);
                return;
            }
            auto* cancellable = pending->owner->recoveryCancellable;
            webkit_website_data_manager_remove(WEBKIT_WEBSITE_DATA_MANAGER(object), WEBKIT_WEBSITE_DATA_ALL,
                                               selected, cancellable, recoveryDataRemoved, pending.release());
            g_list_free_full(selected, reinterpret_cast<GDestroyNotify>(webkit_website_data_unref));
        }, request);
}

void clearBrowsingData(std::function<void(bool)> completion)
{
    if (browsingDataResetInProgress || webview == nullptr)
    {
        if (completion) completion(false);
        return;
    }
    browsingDataResetInProgress = true;
    const auto generation = ++recoveryGeneration;
    recoveryCompletion = std::move(completion);
    recoveryCancellable = g_cancellable_new();
    auto lifetime = deletionChecker;
    choc::messageloop::setTimeout(20000, [this, lifetime, generation] {
        // Only bound the unload phase. Once deletion starts, wait for its native
        // completion so late deletion cannot erase a newly established login.
        if (!lifetime->deleted && browsingDataResetInProgress && recoveryGeneration == generation && recoveryNavigationHandler != 0)
            finishRecovery(false);
    });
    webkit_web_view_stop_loading(WEBKIT_WEB_VIEW(webview));
    recoveryNavigationHandler = g_signal_connect(
        webview, "load-changed",
        G_CALLBACK(+[](WebKitWebView* view, WebKitLoadEvent event, gpointer data) {
            auto* self = static_cast<Pimpl*>(data);
            const char* uri = webkit_web_view_get_uri(view);
            if (event != WEBKIT_LOAD_FINISHED || uri == nullptr || std::string_view(uri) != "about:blank")
                return;
            self->detachRecoverySignals();
            // A running page must not recreate its data while Reset clears it.
            self->clearRecoveryData();
        }), this);
    recoveryFailureHandler = g_signal_connect(
        webview, "load-failed",
        G_CALLBACK(+[](WebKitWebView*, WebKitLoadEvent, const char* uri, GError*, gpointer data) -> gboolean {
            if (uri != nullptr && std::string_view(uri) == "about:blank")
                static_cast<Pimpl*>(data)->finishRecovery(false);
            return FALSE;
        }), this);
    recoveryProcessHandler = g_signal_connect(
        webview, "web-process-terminated",
        G_CALLBACK(+[](WebKitWebView*, WebKitWebProcessTerminationReason, gpointer data) {
            static_cast<Pimpl*>(data)->finishRecovery(false);
        }), this);
    webkit_web_view_load_uri(WEBKIT_WEB_VIEW(webview), "about:blank");
}
