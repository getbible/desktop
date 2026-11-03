// Included inside CHOC's WKWebView::Pimpl by the checked header overlay.
bool browsingDataResetInProgress = false;
bool recoveryWaitingForBlank = false;
std::uint64_t recoveryGeneration = 0;
std::function<void(bool)> recoveryCompletion;

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
