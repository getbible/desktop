#include <platform/choc_Platform.h>

#if CHOC_LINUX
#include <gtk/gtk.h>
#include <webkit2/webkit2.h>
#endif

#include <gui/choc_DesktopWindow.h>
#include <gui/choc_MessageLoop.h>
#include <gui/choc_WebView.h>

#include "app_config.hpp"
#include "app_icon.hpp"

#include <cstdlib>
#include <iostream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace
{
using webapp::config::aboutText;
using webapp::config::aboutURL;
using webapp::config::accentColor;
using webapp::config::allowedOrigins;
using webapp::config::allowedOriginsJSON;
using webapp::config::appURL;
using webapp::config::backgroundColor;
using webapp::config::htmlClass;
using webapp::config::identityJSON;
using webapp::config::javascriptObject;
using webapp::config::name;
using webapp::config::offlineCheckingMessage;
using webapp::config::offlineConnectedMessage;
using webapp::config::offlineFailedMessage;
using webapp::config::offlineMessage;
using webapp::config::offlineRetryLabel;
using webapp::config::offlineTitle;
using webapp::config::offlineURL;
using webapp::config::shellURL;
using webapp::config::showAppMenu;
using webapp::config::slug;
using webapp::config::supportEmail;
using webapp::config::supportLabel;
using webapp::config::supportMessage;
using webapp::config::supportPageURL;
using webapp::config::supportURL;
using webapp::config::supportURLAllowed;
using webapp::config::userAgentProduct;
using webapp::config::version;

#if CHOC_LINUX
constexpr std::string_view appPlatform = "linux";
#elif CHOC_APPLE
constexpr std::string_view appPlatform = "macos";
#elif CHOC_WINDOWS
constexpr std::string_view appPlatform = "windows";
#else
constexpr std::string_view appPlatform = "desktop";
#endif

void replaceAll(std::string& value, const std::string_view needle, const std::string_view replacement)
{
    std::size_t position = 0;

    while ((position = value.find(needle, position)) != std::string::npos)
    {
        value.replace(position, needle.size(), replacement);
        position += replacement.size();
    }
}

std::string htmlEscape(const std::string_view value)
{
    std::string escaped;
    escaped.reserve(value.size());

    for (const char character : value)
    {
        switch (character)
        {
            case '&': escaped += "&amp;"; break;
            case '<': escaped += "&lt;"; break;
            case '>': escaped += "&gt;"; break;
            case '"': escaped += "&quot;"; break;
            case '\'': escaped += "&#39;"; break;
            default: escaped += character; break;
        }
    }

    return escaped;
}

std::string jsonString(const std::string_view value)
{
    std::ostringstream result;
    result << '"';

    for (const unsigned char character : value)
    {
        switch (character)
        {
            case '"': result << "\\\""; break;
            case '\\': result << "\\\\"; break;
            case '\b': result << "\\b"; break;
            case '\f': result << "\\f"; break;
            case '\n': result << "\\n"; break;
            case '\r': result << "\\r"; break;
            case '\t': result << "\\t"; break;
            default:
                if (character < 0x20U)
                {
                    constexpr char hex[] = "0123456789abcdef";
                    result << "\\u00" << hex[(character >> 4U) & 0x0fU] << hex[character & 0x0fU];
                }
                else
                {
                    result << static_cast<char>(character);
                }
                break;
        }
    }

    result << '"';
    return result.str();
}

std::string createPageStyles()
{
    auto styles = std::string(R"CSS(
        :root {
            color-scheme: light dark;
            font-family: system-ui, -apple-system, BlinkMacSystemFont, "Segoe UI", sans-serif;
            background: #f4f6f8;
            color: #17212a;
            --accent: __ACCENT_COLOR__;
            --background: __BACKGROUND_COLOR__;
        }
        * { box-sizing: border-box; }
        body {
            min-height: 100vh;
            margin: 0;
            display: grid;
            place-items: center;
            padding: 2rem;
            background:
                radial-gradient(circle at 20% 10%, color-mix(in srgb, var(--accent) 18%, transparent), transparent 34rem),
                linear-gradient(150deg, #fafbfc, #edf1f4);
        }
        main {
            width: min(36rem, 100%);
            padding: 2.5rem;
            border: 1px solid rgba(23, 33, 42, .13);
            border-radius: 1.25rem;
            background: rgba(255, 255, 255, .9);
            box-shadow: 0 1.25rem 4rem rgba(23, 33, 42, .16);
            text-align: center;
            backdrop-filter: blur(12px);
        }
        .app-icon {
            width: 7rem;
            height: 7rem;
            margin: 0 auto 1.25rem;
            display: block;
            object-fit: contain;
            border-radius: 1rem;
            background: var(--background);
            box-shadow: 0 .75rem 2rem rgba(0, 0, 0, .28);
        }
        h1 { margin: 0 0 .75rem; font-size: clamp(1.7rem, 5vw, 2.25rem); }
        h2 { margin: 1.75rem 0 .5rem; font-size: 1.05rem; }
        p { margin: 0 auto 1.25rem; max-width: 30rem; line-height: 1.65; color: #536878; }
        .meta { font-size: .9rem; }
        .actions { display: flex; flex-wrap: wrap; justify-content: center; gap: .65rem; margin-top: 1.5rem; }
        a, button {
            display: inline-flex;
            align-items: center;
            justify-content: center;
            min-width: 8.5rem;
            padding: .8rem 1.1rem;
            border: 0;
            border-radius: .7rem;
            background: var(--accent);
            color: #121212;
            font: inherit;
            font-weight: 700;
            text-decoration: none;
            cursor: pointer;
        }
        a.secondary, button.secondary { background: #dde5ea; color: #17212a; }
        button:disabled { cursor: wait; opacity: .65; }
        #status { min-height: 1.5rem; margin-top: 1rem; font-size: .92rem; color: #536878; }
        code { overflow-wrap: anywhere; }
        @media (prefers-color-scheme: dark) {
            :root { background: #101820; color: #eef5f8; }
            body { background: radial-gradient(circle at 20% 10%, #27333c, transparent 34rem), #101820; }
            main { background: rgba(20, 31, 40, .94); border-color: rgba(255, 255, 255, .12); }
            p, #status { color: #b8c7d1; }
            a.secondary, button.secondary { background: #34434e; color: #eef5f8; }
        }
    )CSS");

    replaceAll(styles, "__ACCENT_COLOR__", accentColor);
    replaceAll(styles, "__BACKGROUND_COLOR__", backgroundColor);
    return styles;
}

std::string createPage(
    const std::string_view title,
    const std::string_view body,
    const std::string_view actions,
    const std::string_view script = {})
{
    std::ostringstream page;
    page << "<!doctype html>\n"
         << "<html lang=\"en\">\n<head>\n"
         << "<meta charset=\"utf-8\">\n"
         << "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">\n"
         << "<meta name=\"color-scheme\" content=\"light dark\">\n"
         << "<title>" << htmlEscape(name) << " — " << htmlEscape(title) << "</title>\n"
         << "<style>" << createPageStyles() << "</style>\n"
         << "</head>\n<body>\n<main>\n"
         << "<img class=\"app-icon\" src=\"/icon.png\" alt=\"\">\n"
         << "<h1>" << htmlEscape(title) << "</h1>\n"
         << body << "\n"
         << "<div class=\"actions\">" << actions << "</div>\n"
         << "</main>\n";

    if (!script.empty())
    {
        page << "<script>" << script << "</script>\n";
    }

    page << "</body>\n</html>\n";
    return page.str();
}

#if CHOC_LINUX
bool hasAllowedOrigin(const std::string_view value)
{
    for (const auto origin : allowedOrigins)
    {
        if (value == origin)
        {
            return true;
        }
    }

    return false;
}

std::optional<std::string> getLinuxOrigin(const char* uri)
{
    if (uri == nullptr || *uri == '\0')
    {
        return std::nullopt;
    }

    GError* error = nullptr;
    GUri* parsedURI = g_uri_parse(uri, G_URI_FLAGS_PARSE_RELAXED, &error);

    if (parsedURI == nullptr)
    {
        g_clear_error(&error);
        return std::nullopt;
    }

    const char* scheme = g_uri_get_scheme(parsedURI);
    const char* host = g_uri_get_host(parsedURI);
    const int port = g_uri_get_port(parsedURI);
    std::optional<std::string> origin;

    if (scheme != nullptr && host != nullptr)
    {
        std::ostringstream value;
        value << scheme << "://" << host;
        if (port != -1 && port != 443)
        {
            value << ':' << port;
        }
        origin = value.str();
    }

    g_uri_unref(parsedURI);
    return origin;
}
#endif
