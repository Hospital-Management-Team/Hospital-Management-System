#include "core/text.hpp"

namespace hms::text {

bool isValidUtf8(std::string_view s) {
    std::size_t i = 0;
    const std::size_t n = s.size();
    while (i < n) {
        const auto c = static_cast<unsigned char>(s[i]);
        std::size_t len;
        unsigned int cp;
        if (c < 0x80) {
            ++i;
            continue;
        } else if ((c & 0xE0) == 0xC0) {
            len = 2;
            cp = c & 0x1Fu;
        } else if ((c & 0xF0) == 0xE0) {
            len = 3;
            cp = c & 0x0Fu;
        } else if ((c & 0xF8) == 0xF0) {
            len = 4;
            cp = c & 0x07u;
        } else {
            return false;
        }
        if (i + len > n) return false;
        for (std::size_t k = 1; k < len; ++k) {
            const auto cc = static_cast<unsigned char>(s[i + k]);
            if ((cc & 0xC0) != 0x80) return false;
            cp = (cp << 6) | (cc & 0x3Fu);
        }
        // Reject overlong encodings, surrogates and values above U+10FFFF.
        if ((len == 2 && cp < 0x80) || (len == 3 && cp < 0x800) || (len == 4 && cp < 0x10000)) return false;
        if (cp >= 0xD800 && cp <= 0xDFFF) return false;
        if (cp > 0x10FFFF) return false;
        i += len;
    }
    return true;
}

std::size_t utf8Length(std::string_view s) {
    std::size_t count = 0;
    for (char ch : s)
        if ((static_cast<unsigned char>(ch) & 0xC0) != 0x80) ++count;
    return count;
}

bool containsControlChars(std::string_view s) {
    for (std::size_t i = 0; i < s.size(); ++i) {
        const auto c = static_cast<unsigned char>(s[i]);
        if (c < 0x20 || c == 0x7F) return true;
        // C1 controls are encoded as 0xC2 0x80..0x9F in UTF-8.
        if (c == 0xC2 && i + 1 < s.size()) {
            const auto next = static_cast<unsigned char>(s[i + 1]);
            if (next >= 0x80 && next <= 0x9F) return true;
        }
    }
    return false;
}

std::string trimSpaces(std::string_view s) {
    std::size_t b = 0, e = s.size();
    while (b < e && s[b] == ' ') ++b;
    while (e > b && s[e - 1] == ' ') --e;
    return std::string(s.substr(b, e - b));
}

std::string collapseSpaces(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    bool prevSpace = false;
    for (char c : s) {
        if (c == ' ') {
            if (!prevSpace) out.push_back(' ');
            prevSpace = true;
        } else {
            out.push_back(c);
            prevSpace = false;
        }
    }
    return out;
}

std::string asciiLower(std::string_view s) {
    std::string out(s);
    for (char& c : out)
        if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
    return out;
}

std::string normaliseName(std::string_view s) { return asciiLower(collapseSpaces(trimSpaces(s))); }

std::string fitWidth(std::string_view s, std::size_t width) {
    const std::size_t len = utf8Length(s);
    if (len <= width) return std::string(s) + std::string(width - len, ' ');
    if (width <= 3) return std::string(width, '.');
    // Cut at a code-point boundary leaving room for "...".
    std::size_t keep = width - 3, cps = 0, i = 0;
    while (i < s.size()) {
        if ((static_cast<unsigned char>(s[i]) & 0xC0) != 0x80) {
            if (cps == keep) break;
            ++cps;
        }
        ++i;
    }
    return std::string(s.substr(0, i)) + "...";
}

}  // namespace hms::text
