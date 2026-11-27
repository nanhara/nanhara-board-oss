#pragma once

#include <charconv>
#include <string_view>

namespace board_update
{
inline constexpr unsigned current_preview = 11;

inline bool is_newer_preview(std::string_view version, unsigned current) noexcept
{
    constexpr std::string_view prefix = "0.2.0-alpha.";
    if (version.ends_with("\r\n"))
        version.remove_suffix(2);
    else if (version.ends_with("\n"))
        version.remove_suffix(1);
    if (!version.starts_with(prefix))
        return false;
    version.remove_prefix(prefix.size());
    if (version.empty() || version.front() < '1' || version.front() > '9')
        return false;
    unsigned number{};
    const auto end = version.data() + version.size();
    const auto parsed = std::from_chars(version.data(), end, number);
    return parsed.ec == std::errc{} && parsed.ptr == end && number > current;
}
}
