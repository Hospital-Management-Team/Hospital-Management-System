#pragma once
// Printed identifiers such as LAB-000012 (issue #8). One number per record; the
// prefix says what kind of record it is. Staff may type the number with or
// without the prefix.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "core/text.hpp"

namespace hms {

inline std::string formatNumbered(const char* prefix, std::int64_t id) {
    std::string digits = std::to_string(id);
    if (digits.size() < 6) digits.insert(0, 6 - digits.size(), '0');
    return std::string(prefix) + "-" + digits;
}

inline std::optional<std::int64_t> parseNumbered(std::string_view raw, const char* prefix) {
    std::string t = text::asciiLower(text::trimSpaces(std::string(raw)));
    const std::string p = text::asciiLower(prefix) + "-";
    if (t.rfind(p, 0) == 0) t = t.substr(p.size());
    if (t.empty() || t.size() > 12) return std::nullopt;
    std::int64_t n = 0;
    for (char c : t) {
        if (c < '0' || c > '9') return std::nullopt;
        n = n * 10 + (c - '0');
    }
    if (n <= 0) return std::nullopt;
    return n;
}

}  // namespace hms
