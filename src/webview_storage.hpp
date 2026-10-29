#pragma once

#include "app_config.hpp"
#include <platform/choc_Platform.h>
#include <string>

#if CHOC_LINUX
#include <glib/gstdio.h>
#include <webkit2/webkit2.h>

namespace webapp::storage
{
inline WebKitWebContext* createLinuxContext()
{
    if (!config::persistent)
    {
        auto* manager = webkit_website_data_manager_new_ephemeral();
        auto* context = webkit_web_context_new_with_website_data_manager(manager);
        g_object_unref(manager);
        return context;
    }

    const std::string appID(config::appID);
    // Preserve the original GetBible application's existing WebKit data/cache
    // locations. New application identities receive separate browser profiles.
    const bool keepDefaultDirectories = appID == "live.getbible.app";
    gchar* dataDirectory = g_build_filename(g_get_user_data_dir(), appID.c_str(), nullptr);
    gchar* cacheDirectory = g_build_filename(g_get_user_cache_dir(), appID.c_str(), nullptr);
    if (g_mkdir_with_parents(dataDirectory, 0700) != 0 ||
        g_chmod(dataDirectory, 0700) != 0 ||
        (!keepDefaultDirectories && (g_mkdir_with_parents(cacheDirectory, 0700) != 0 ||
                                     g_chmod(cacheDirectory, 0700) != 0)))
    {
        g_printerr("Unable to create private browser storage for %s.\n", appID.c_str());
        g_free(dataDirectory);
        g_free(cacheDirectory);
        return nullptr;
    }

    auto* manager = keepDefaultDirectories
        ? webkit_website_data_manager_new(nullptr)
        : webkit_website_data_manager_new(
            "base-data-directory", dataDirectory, "base-cache-directory", cacheDirectory, nullptr);
    auto* context = webkit_web_context_new_with_website_data_manager(manager);
    webkit_web_context_set_cache_model(context, WEBKIT_CACHE_MODEL_WEB_BROWSER);
    gchar* cookieDatabase = g_build_filename(dataDirectory, "cookies.sqlite", nullptr);
    webkit_cookie_manager_set_persistent_storage(
        webkit_website_data_manager_get_cookie_manager(manager), cookieDatabase,
        WEBKIT_COOKIE_PERSISTENT_STORAGE_SQLITE);

    g_free(cookieDatabase);
    g_object_unref(manager);
    g_free(dataDirectory);
    g_free(cacheDirectory);
    return context;
}
} // namespace webapp::storage

#elif CHOC_APPLE
#include <platform/choc_ObjectiveCHelpers.h>

namespace webapp::storage
{
inline bool configureAppleDataStore(id configuration)
{
    // The default store is isolated by the app's generated CFBundleIdentifier.
    // Nonpersistent stores are available on every supported macOS version.
    const char* selector = config::persistent ? "defaultDataStore" : "nonPersistentDataStore";
    id store = choc::objc::callClass<id>("WKWebsiteDataStore", selector);
    if (store == nullptr)
        return false;
    choc::objc::call<void>(configuration, "setWebsiteDataStore:", store);
    return true;
}
} // namespace webapp::storage

#elif CHOC_WINDOWS
#include <windows.h>
#include <shlobj.h>
#include <cstdio>
#include <filesystem>
#include <system_error>

namespace webapp::storage
{
class WindowsProfile
{
public:
    WindowsProfile()
    {
        // Persistent mode keeps CHOC's existing AppData/executable-name path.
        // This helper owns only temporary profiles, so upgrades retain logins.
        if (config::persistent)
            return;
        // Configuration validates application IDs as ASCII identifiers.
        const std::wstring appID(config::appID.begin(), config::appID.end());
        std::error_code error;

        // A new folder on every launch guarantees no previous session is read.
        // This is temporary disk storage, not WebView2 InPrivate mode.
        const auto temporaryRoot = std::filesystem::temp_directory_path(error);
        if (error)
            return;
        for (int attempt = 0; attempt < 3; ++attempt)
        {
            GUID identifier {};
            wchar_t identifierText[40] {};
            if (FAILED(CoCreateGuid(&identifier)) || StringFromGUID2(identifier, identifierText, 40) == 0)
                return;
            const auto candidate = temporaryRoot / (appID + L"-" + identifierText);
            error.clear();
            if (std::filesystem::create_directory(candidate, error))
            {
                directory = candidate;
                return;
            }
            if (error)
                return;
        }
    }

    WindowsProfile(const WindowsProfile&) = delete;
    WindowsProfile& operator=(const WindowsProfile&) = delete;
    ~WindowsProfile() { cleanup(); }

    std::wstring path() const { return directory.wstring(); }
    void browserStarted() { started = true; }

    void captureBrowserProcess(DWORD processID)
    {
        if (!config::persistent && processID != 0 && browserProcess == nullptr)
            browserProcess = OpenProcess(SYNCHRONIZE, FALSE, processID);
    }

    void cleanup()
    {
        if (cleanupAttempted || config::persistent || directory.empty())
            return;
        cleanupAttempted = true;

        // WebView2 can outlive its host objects. Delete only after its process
        // exits, never while its profile might still be in use. An interrupted
        // shutdown may leave temporary files, which are never used next launch.
        const bool stopped = !started ||
            (browserProcess != nullptr && WaitForSingleObject(browserProcess, 5000) == WAIT_OBJECT_0);
        if (browserProcess != nullptr)
        {
            CloseHandle(browserProcess);
            browserProcess = nullptr;
        }
        if (stopped)
        {
            std::error_code error;
            std::filesystem::remove_all(directory, error);
            if (!error)
                return;
        }
        std::fputs("Temporary browser data could not be removed after shutdown; it will not be reused.\n", stderr);
    }

private:
    std::filesystem::path directory;
    HANDLE browserProcess = nullptr;
    bool started = false;
    bool cleanupAttempted = false;
};
} // namespace webapp::storage
#endif
