#pragma once

#include "app_config.hpp"
#include <algorithm>
#include <string>
#include <string_view>

namespace webapp::navigation
{
// The native engines supply an absolute, serialized URL for each policy decision.
// Match its entire origin, independently of the path, query, or fragment.
inline bool allowsTopLevelURL(std::string_view url)
{
    if (url == "about:blank")
        return true;  // Reset unloads the current website before clearing its data.

    const bool blob = url.substr(0, 5) == "blob:";
    if (blob)
        url.remove_prefix(5);

    const auto separator = url.find("://");
    if (separator == std::string_view::npos)
        return false;

    auto authority = url.substr(separator + 3);
    authority = authority.substr(0, authority.find_first_of("/?#"));
    if (authority.empty() || authority.find('\\') != std::string_view::npos)
        return false;
    if (const auto at = authority.rfind('@'); at != std::string_view::npos)
        authority.remove_prefix(at + 1);

    std::string origin(url.substr(0, separator + 3));
    origin.append(authority);
    std::transform(origin.begin(), origin.end(), origin.begin(), [](char c) {
        return c >= 'A' && c <= 'Z' ? static_cast<char>(c + ('a' - 'A')) : c;
    });
    if (!blob && origin == config::shellURL)
        return true;
    if (origin.compare(0, 8, "https://") != 0)
        return false;
    if (origin.size() >= 4 && origin.compare(origin.size() - 4, 4, ":443") == 0)
        origin.resize(origin.size() - 4);
    return std::find(config::allowedOrigins.begin(), config::allowedOrigins.end(), origin)
        != config::allowedOrigins.end();
}
} // namespace webapp::navigation
