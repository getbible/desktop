#!/usr/bin/env python3
"""Prepare a project-owned CHOC header overlay without changing its checkout.

The pinned CHOC WebView has no storage options. This adapter inserts native
storage hooks before each platform creates its browser and checks every anchor
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
