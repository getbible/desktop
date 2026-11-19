// Included inside CHOC's WebView2::Pimpl by the checked header overlay.
bool browsingDataResetInProgress = false;
std::uint64_t recoveryGeneration = 0;
bool recoveryNavigationRegistered = false;
EventRegistrationToken recoveryNavigationToken{};
std::function<void(bool)> recoveryCompletion;

// CHOC's self-contained WebView2 declarations use void* callback parameters.
// These are the native COM callback vtables from Microsoft's WebView2 SDK.
struct RecoveryNavigationCallback : public IUnknown
{
    Pimpl& owner;
    std::shared_ptr<DeletionChecker> lifetime;
    std::uint64_t generation;
    std::atomic<ULONG> references{0};
    explicit RecoveryNavigationCallback(Pimpl& p) : owner(p), lifetime(p.deletionChecker), generation(p.recoveryGeneration) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** result) override
    {
        if (iid == IID_IUnknown || iid == IID{0xd33a35bf, 0x1c49, 0x4f98, {0x93, 0xab, 0x00, 0x6e, 0x05, 0x33, 0xfe, 0x1c}})
        { *result = this; AddRef(); return S_OK; }
        *result = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references; }
    ULONG STDMETHODCALLTYPE Release() override { auto count = --references; if (count == 0) delete this; return count; }
    virtual HRESULT STDMETHODCALLTYPE Invoke(ICoreWebView2*, void*)
    {
        // Removing an event handler can release the event's last reference.
        COMPtr<RecoveryNavigationCallback> keepAlive(this);
        if (!lifetime->deleted && owner.browsingDataResetInProgress && owner.recoveryGeneration == generation)
            owner.finishRecoveryNavigation();
        return S_OK;
    }
};

struct RecoveryProtocolCallback : public IUnknown
{
    std::function<void(bool)> callback;
    std::atomic<ULONG> references{0};
    explicit RecoveryProtocolCallback(std::function<void(bool)> cb) : callback(std::move(cb)) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID iid, void** result) override
    {
        if (iid == IID_IUnknown || iid == IID{0x5c4889f0, 0x5ef6, 0x4c5a, {0x95, 0x2c, 0xd8, 0xf1, 0xb9, 0x2d, 0x05, 0x74}})
        { *result = this; AddRef(); return S_OK; }
        *result = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references; }
    ULONG STDMETHODCALLTYPE Release() override { auto count = --references; if (count == 0) delete this; return count; }
    virtual HRESULT STDMETHODCALLTYPE Invoke(HRESULT result, LPCWSTR)
    {
        if (callback) callback(SUCCEEDED(result));
        return S_OK;
    }
};

bool reload(bool ignoreCache)
{
    if (browsingDataResetInProgress || !coreWebView)
        return false;
    LPWSTR source = nullptr;
    const auto result = coreWebView->get_Source(&source);
    std::string uri = source != nullptr ? createUTF8FromUTF16(std::wstring(source)) : "";
    CoTaskMemFree(source);
    if (FAILED(result)) return false;
    if (webapp::recovery::refreshNeedsHome(uri))
        return navigate(std::string(webapp::config::appURL));
    if (ignoreCache)
        return SUCCEEDED(coreWebView->CallDevToolsProtocolMethod(L"Page.reload", L"{\"ignoreCache\":true}", nullptr));
    return SUCCEEDED(coreWebView->Reload());
}

void disconnectRecoveryNavigation()
{
    if (recoveryNavigationRegistered && coreWebView)
        coreWebView->remove_NavigationCompleted(recoveryNavigationToken);
    recoveryNavigationRegistered = false;
}

void finishRecovery(bool success)
{
    if (!browsingDataResetInProgress) return;
    disconnectRecoveryNavigation();
    browsingDataResetInProgress = false;
    auto callback = std::move(recoveryCompletion);
    recoveryCompletion = {};
    if (callback) callback(success);
}

void clearBrowsingData(std::function<void(bool)> completion)
{
    if (browsingDataResetInProgress || !coreWebView)
    {
        if (completion) completion(false);
        return;
    }
    browsingDataResetInProgress = true;
    const auto generation = ++recoveryGeneration;
    recoveryCompletion = std::move(completion);
    auto lifetime = deletionChecker;
    choc::messageloop::setTimeout(20000, [this, lifetime, generation] {
        // Bound unloading only. CDP data removal must finish before fresh login.
        if (!lifetime->deleted && browsingDataResetInProgress && recoveryGeneration == generation && recoveryNavigationRegistered)
            finishRecovery(false);
    });
    coreWebView->Stop();
    COMPtr<RecoveryNavigationCallback> callback(new RecoveryNavigationCallback(*this));
    if (FAILED(coreWebView->add_NavigationCompleted(callback, &recoveryNavigationToken)))
    {
        finishRecovery(false);
        return;
    }
    recoveryNavigationRegistered = true;
    if (!navigate("about:blank")) finishRecovery(false);
}

struct RecoveryCommands
{
    std::vector<std::pair<std::wstring, std::wstring>> commands;
    std::size_t position = 0;
    std::uint64_t generation = 0;
};

void finishRecoveryNavigation()
{
    if (!browsingDataResetInProgress || !recoveryNavigationRegistered)
        return;
    LPWSTR source = nullptr;
    const bool blank = SUCCEEDED(coreWebView->get_Source(&source)) && source != nullptr && std::wstring_view(source) == L"about:blank";
    CoTaskMemFree(source);
    if (!blank) return;
    disconnectRecoveryNavigation();
    auto commands = std::make_shared<RecoveryCommands>();
    commands->generation = recoveryGeneration;
    // Cookies and HTTP cache belong to this executable's isolated profile.
    commands->commands.emplace_back(L"Network.clearBrowserCache", L"{}");
    commands->commands.emplace_back(L"Network.clearBrowserCookies", L"{}");
    for (auto origin : webapp::config::allowedOrigins)
    {
        auto json = std::string("{\"origin\":") + choc::json::getEscapedQuotedString(origin) + ",\"storageTypes\":\"all\"}";
        commands->commands.emplace_back(L"Storage.clearDataForOrigin", createUTF16StringFromUTF8(json));
    }
    runRecoveryCommand(commands);
}

void runRecoveryCommand(std::shared_ptr<RecoveryCommands> commands)
{
    if (!browsingDataResetInProgress || commands->generation != recoveryGeneration)
        return;
    if (commands->position == commands->commands.size())
    {
        finishRecovery(true);
        return;
    }
    const auto& command = commands->commands[commands->position++];
    auto lifetime = deletionChecker;
    COMPtr<RecoveryProtocolCallback> callback(new RecoveryProtocolCallback(
        [this, lifetime, commands](bool success) {
            if (lifetime->deleted || !browsingDataResetInProgress || commands->generation != recoveryGeneration) return;
            if (success) runRecoveryCommand(commands);
            else finishRecovery(false);
        }));
    if (FAILED(coreWebView->CallDevToolsProtocolMethod(command.first.c_str(), command.second.c_str(), callback)))
        finishRecovery(false);
}
