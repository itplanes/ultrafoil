#pragma once
#include <charconv>
#include <cstdint>
#include <limits>
#include <string_view>

namespace inst::http {
    inline bool ValidContentRange(std::string_view value, std::uint64_t offset, std::uint64_t size) {
        while (!value.empty() && (value.back() == '\r' || value.back() == '\n' || value.back() == ' ')) value.remove_suffix(1);
        while (!value.empty() && value.front() == ' ') value.remove_prefix(1);
        if (value.substr(0, 6) != "bytes " || !size || offset > std::numeric_limits<std::uint64_t>::max() - (size - 1)) return false;
        value.remove_prefix(6);
        const auto dash = value.find('-'), slash = value.find('/');
        if (dash == value.npos || slash == value.npos || slash <= dash) return false;
        const auto number = [](std::string_view text, std::uint64_t& result) {
            const auto parsed = std::from_chars(text.data(), text.data() + text.size(), result);
            return parsed.ec == std::errc() && parsed.ptr == text.data() + text.size();
        };
        std::uint64_t first = 0, last = 0, total = 0;
        if (!number(value.substr(0, dash), first) || !number(value.substr(dash + 1, slash - dash - 1), last)) return false;
        if (first != offset || last != offset + size - 1) return false;
        const auto totalText = value.substr(slash + 1);
        return totalText == "*" || (number(totalText, total) && total > last);
    }
}
