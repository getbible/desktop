#pragma once

#include <string_view>
#include <cstdint>
#include <utility>
#include <gui/choc_MessageLoop.h>

namespace webapp::recovery
{
// Refresh always offers a route back from an embedded/error page to the website.
inline bool refreshNeedsHome(std::string_view uri)
{
    return uri.empty() || uri.rfind("webapp:", 0) == 0 || uri.rfind("about:", 0) == 0;
}
} // namespace webapp::recovery
