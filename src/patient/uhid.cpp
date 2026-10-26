#include "patient/uhid.hpp"

#include <cstdio>

#include "core/text.hpp"

namespace hms {

std::string formatUhid(std::int64_t uhid) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "HMS-%06lld", static_cast<long long>(uhid));
    return buf;
}

std::optional<std::int64_t> parseUhid(std::string_view text) {
    std::string s = text::trimSpaces(text);
    std::string_view v = s;
    if (v.size() >= 3 && text::asciiLower(v.substr(0, 3)) == "hms") {
        v.remove_prefix(3);
        if (!v.empty() && v.front() == '-') v.remove_prefix(1);
    }
    if (v.empty() || v.size() > 18) return std::nullopt;
    std::int64_t value = 0;
    for (char c : v) {
        if (c < '0' || c > '9') return std::nullopt;
        value = value * 10 + (c - '0');
    }
    if (value <= 0) return std::nullopt;
    return value;
}

}  // namespace hms
