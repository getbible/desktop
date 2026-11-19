// Included inside CHOC's WKWebView::Pimpl by the checked header overlay.
bool browsingDataResetInProgress = false;
bool recoveryWaitingForBlank = false;
std::uint64_t recoveryGeneration = 0;
std::function<void(bool)> recoveryCompletion;

static bool allowsAppleNavigation(id requestOrResponse)
{
    id url = objc::call<id>(requestOrResponse, "URL");
    return url != nullptr && webapp::navigation::allowsTopLevelURL(
        objc::getString(objc::call<id>(url, "absoluteString")));
}

static void registerNavigationPolicy(Class delegateClass)
{
    // Both WKNavigationActionPolicy and WKNavigationResponsePolicy are
    // NSInteger enums with these values on every supported macOS version.
    constexpr long cancel = 0, allow = 1;
    class_addMethod(delegateClass, sel_registerName("webView:decidePolicyForNavigationAction:decisionHandler:"),
        (IMP) (+[](id self, SEL, id, id action, void (^decide)(long)) {
            id frame = objc::call<id>(action, "targetFrame");
            // New windows have no target frame. Allowed links that the page
            // opens in the current window go through the normal main-frame path.
            const bool permitted = getPimpl(self) != nullptr && frame != nullptr
                && (!objc::call<BOOL>(frame, "isMainFrame")
                    || allowsAppleNavigation(objc::call<id>(action, "request")));
            decide(permitted ? allow : cancel);
        }), "v@:@@@?");

    class_addMethod(delegateClass, sel_registerName("webView:decidePolicyForNavigationResponse:decisionHandler:"),
        (IMP) (+[](id self, SEL, id, id navigationResponse, void (^decide)(long)) {
            // Check the final response URL before WebKit commits a redirect.
            // Keep WebKit's normal rejection of MIME types it cannot display.
            const bool permitted = getPimpl(self) != nullptr
                && objc::call<BOOL>(navigationResponse, "canShowMIMEType")
                && (!objc::call<BOOL>(navigationResponse, "isForMainFrame")
                    || allowsAppleNavigation(objc::call<id>(navigationResponse, "response")));
            decide(permitted ? allow : cancel);
        }), "v@:@@@?");
}

static bool wasInterruptedByNavigationPolicy(id error)
{
    // A cancelled navigation can produce this delegate error in addition to
    // NSURLErrorCancelled. Rendering CHOC's error page would discard the form.
    constexpr long frameLoadInterruptedByPolicyChange = 102;
    return objc::call<long>(error, "code") == frameLoadInterruptedByPolicyChange
        && objc::getString(objc::call<id>(error, "domain")) == "WebKitErrorDomain";
}

bool reload(bool ignoreCache)
{
    if (browsingDataResetInProgress || webview == nullptr)
        return false;
    auto uri = objc::getString(objc::call<id>(objc::call<id>(webview, "URL"), "absoluteString"));
    if (webapp::recovery::refreshNeedsHome(uri))
        return navigate(std::string(webapp::config::appURL));
    return objc::call<id>(webview, ignoreCache ? "reloadFromOrigin" : "reload") != nullptr;
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
    recoveryWaitingForBlank = true;
    recoveryCompletion = std::move(completion);
    auto lifetime = deletionChecker;
    choc::messageloop::setTimeout(20000, [this, lifetime, generation] {
        // WKWebsiteDataStore deletion cannot be cancelled; its completion must
        // precede browsing again to avoid deleting a newly established login.
        if (!lifetime->deleted && browsingDataResetInProgress && recoveryGeneration == generation && recoveryWaitingForBlank)
            finishRecovery(false);
    });
    objc::call<void>(webview, "stopLoading");
    if (!navigate("about:blank"))
        finishRecovery(false);
}

void finishRecovery(bool success)
{
    if (!browsingDataResetInProgress) return;
    browsingDataResetInProgress = false;
    recoveryWaitingForBlank = false;
    auto callback = std::move(recoveryCompletion);
    recoveryCompletion = {};
    if (callback) callback(success);
}

void finishRecoveryNavigation()
{
    if (!browsingDataResetInProgress || !recoveryWaitingForBlank)
        return;
    auto uri = objc::getString(objc::call<id>(objc::call<id>(webview, "URL"), "absoluteString"));
    if (uri != "about:blank")
        return;
    recoveryWaitingForBlank = false;
    id configuration = objc::call<id>(webview, "configuration");
    id store = objc::call<id>(configuration, "websiteDataStore");
    id types = objc::callClass<id>("WKWebsiteDataStore", "allWebsiteDataTypes");
    id beginning = objc::callClass<id>("NSDate", "distantPast");
    auto lifetime = deletionChecker;
    const auto generation = recoveryGeneration;
    objc::call<void>(store, "removeDataOfTypes:modifiedSince:completionHandler:", types, beginning,
                    ^() {
                        choc::messageloop::postMessage([this, lifetime, generation] {
                            if (!lifetime->deleted && browsingDataResetInProgress && recoveryGeneration == generation)
                                finishRecovery(true);
                        });
                    });
}
