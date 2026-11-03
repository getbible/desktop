#!/usr/bin/env python3
"""Prepare a project-owned CHOC header overlay without changing its checkout.

The pinned CHOC WebView has no storage or recovery options. This adapter inserts
native hooks before each platform creates its browser and checks every anchor
so an incompatible dependency update fails explicitly during configuration.
"""
from __future__ import annotations

import argparse
from pathlib import Path
import shutil
import sys


def replace_once(source: str, old: str, new: str, label: str) -> str:
    if source.count(old) != 1:
        raise ValueError(f"Unsupported CHOC WebView: {label} anchor changed")
    return source.replace(old, new, 1)


def add_recovery_support(header: str) -> str:
    """Expose native recovery without exposing browser-profile paths to pages."""
    header = replace_once(header,
                          '#include "webview_storage.hpp"',
                          '#include "webview_storage.hpp"\n#include "webview_recovery.hpp"',
                          "recovery include")
    declaration = '    bool navigate (const std::string& url);'
    header = replace_once(header, declaration, declaration + '''

    /// Reloads the current website; embedded/error pages return to the app URL.
    /// ignoreCache preserves cookies and website storage while fetching fresh resources.
    bool reload (bool ignoreCache = false);

    /// Unloads the page and asynchronously clears this app's browsing data.
    /// Call on the UI thread. Completion runs there and is suppressed after destruction.
    /// On Windows, website storage is cleared for every configured allowed origin.
    void clearBrowsingData (std::function<void(bool)> completion);''',
                          "recovery API")
    definition = 'inline bool WebView::navigate (const std::string& url)               { return pimpl != nullptr && pimpl->navigate (url); }'
    header = replace_once(header, definition, definition + '''
inline bool WebView::reload (bool ignoreCache)                       { return pimpl != nullptr && pimpl->reload (ignoreCache); }
inline void WebView::clearBrowsingData (std::function<void(bool)> completion)
{
    if (pimpl != nullptr)
        pimpl->clearBrowsingData (std::move (completion));
    else if (completion)
        completion (false);
}''', "recovery API definitions")

    # Scope the repeated native class anchors to their platform sections.
    linux, apple_and_windows = header.split('#elif CHOC_APPLE', 1)
    apple, windows = apple_and_windows.split('#elif CHOC_WINDOWS', 1)
    marker = '    std::shared_ptr<DeletionChecker> deletionChecker { std::make_shared<DeletionChecker>() };'
    linux = replace_once(linux, marker, marker + '\n\n#include "webview_recovery_linux.inc"', "Linux recovery methods")
    apple = replace_once(apple, marker, marker + '\n\n#include "webview_recovery_apple.inc"', "macOS recovery methods")
    windows = replace_once(windows, marker, marker + '\n\n#include "webview_recovery_windows.inc"', "Windows recovery methods")
    linux = replace_once(linux,
                         '        deletionChecker->deleted = true;',
                         '        deletionChecker->deleted = true;\n        disconnectRecoveryNavigation();',
                         "Linux recovery shutdown")
    windows = replace_once(windows,
                           '        deletionChecker->deleted = true;',
                           '        deletionChecker->deleted = true;\n        disconnectRecoveryNavigation();',
                           "Windows recovery shutdown")
    delegate_marker = '            class_addMethod (delegateClass, sel_registerName ("webView:stopURLSchemeTask:"), (IMP) (+[](id, SEL, id, id) {}), "v@:@@");'
    apple = replace_once(apple, delegate_marker, '''            class_addMethod (delegateClass, sel_registerName ("webView:didFinishNavigation:"),
                             (IMP) (+[](id self, SEL, id, id)
                             {
                                 if (auto p = getPimpl (self))
                                     p->finishRecoveryNavigation();
                             }), "v@:@@");

''' + delegate_marker, "macOS recovery navigation")
    error_marker = '''        if (objc::call<int> (error, "code") == NSURLErrorCancelled)
            return;'''
    apple = replace_once(apple, error_marker, error_marker + '''

        if (browsingDataResetInProgress && recoveryWaitingForBlank)
            finishRecovery (false);''', "macOS recovery failure")
    return linux + '#elif CHOC_APPLE' + apple + '#elif CHOC_WINDOWS' + windows


def prepare(source_root: Path, output_root: Path) -> None:
    source_root = source_root.resolve()
    output_root = output_root.resolve()
    if source_root == output_root or source_root in output_root.parents:
        raise ValueError("The prepared CHOC directory must be outside its source tree")
    if not (source_root / "platform/choc_Platform.h").is_file():
        raise ValueError("--source must be the CHOC header root containing gui/ and platform/")
    header = (source_root / "gui/choc_WebView.h").read_text(encoding="utf-8")
    header = replace_once(header, '#include "../platform/choc_Platform.h"',
                          '#include "../platform/choc_Platform.h"\n#include "webview_storage.hpp"',
                          "storage include")
    header = replace_once(header, '        webviewContext = webkit_web_context_new();',
                          '        webviewContext = webapp::storage::createLinuxContext();\n'
                          '        if (webviewContext == nullptr)\n            return false;',
                          "Linux context")
    header = replace_once(header,
                          '        webkit_settings_set_javascript_can_access_clipboard (settings, true);',
                          '        webkit_settings_set_javascript_can_access_clipboard (settings, true);\n'
                          '        webkit_settings_set_enable_html5_local_storage (settings, true);',
                          "Linux local storage")
    header = replace_once(header,
                          '        id config = callClass<id> ("WKWebViewConfiguration", "new");',
                          '        id config = callClass<id> ("WKWebViewConfiguration", "new");\n'
                          '        if (! webapp::storage::configureAppleDataStore (config))\n'
                          '        {\n            call<void> (config, "release");\n            return false;\n        }',
                          "macOS data store")
    header = replace_once(header,
                          '                if (auto userDataFolder = getUserDataFolder(); ! userDataFolder.empty())',
                          '                if (auto userDataFolder = webapp::config::persistent ? getUserDataFolder() : storageProfile.path(); ! userDataFolder.empty())',
                          "Windows profile selection")
    header = replace_once(header,
                          '                        createCoreWebView2EnvironmentWithOptions (nullptr, userDataFolder.c_str(), nullptr, eventHandler);\n                        return true;',
                          '                        const auto result = createCoreWebView2EnvironmentWithOptions (nullptr, userDataFolder.c_str(), nullptr, eventHandler);\n'
                          '                        if (FAILED (result))\n                            return false;\n'
                          '                        storageProfile.browserStarted();\n                        return true;',
                          "Windows environment")
    old_destructor = '''        eventHandler.reset();
        coreWebView.reset();
        coreWebViewController.reset();
        coreWebViewEnvironment.reset();
        hwnd.reset();'''
    new_destructor = '''        if (! webapp::config::persistent)
        {
            if (coreWebView != nullptr)
            {
                UINT32 browserProcessID = 0;
                if (SUCCEEDED (coreWebView->get_BrowserProcessId (&browserProcessID)))
                    storageProfile.captureBrowserProcess (browserProcessID);
            }
            if (coreWebViewController != nullptr)
                coreWebViewController->Close();
        }
        eventHandler.reset();
        coreWebView.reset();
        coreWebViewController.reset();
        coreWebViewEnvironment.reset();
        hwnd.reset();
        storageProfile.cleanup();'''
    header = replace_once(header, old_destructor, new_destructor, "Windows shutdown")
    header = replace_once(header, '    Options options;\n    COMPtr<EventHandler> eventHandler;',
                          '    Options options;\n    webapp::storage::WindowsProfile storageProfile;\n    COMPtr<EventHandler> eventHandler;',
                          "Windows profile member")
    method_start = '    static std::wstring getUserDataFolder()\n    {'
    method_end = '\n};\n\n} // namespace choc::ui'
    if header.count(method_start) != 1:
        raise ValueError("Unsupported CHOC WebView: Windows profile method changed")
    start = header.index(method_start)
    end = header.find(method_end, start)
    if end < 0 or 'return path + currentExeName;' not in header[start:end]:
        raise ValueError("Unsupported CHOC WebView: Windows profile method body changed")
    # Keep the original persistent AppData/executable-name implementation intact.

    header = add_recovery_support(header)

    # Only write output after every platform's hooks have been validated.
    output_root.mkdir(parents=True, exist_ok=True)
    shutil.copytree(source_root, output_root, dirs_exist_ok=True)
    (output_root / "gui/choc_WebView.h").write_text(header, encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    try:
        prepare(args.source, args.output)
    except (OSError, ValueError) as error:
        print(f"Unable to prepare CHOC storage support: {error}", file=sys.stderr)
        return 1
    print(f"Prepared CHOC storage support in {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
