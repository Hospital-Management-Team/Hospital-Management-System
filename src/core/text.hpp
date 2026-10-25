#pragma once
// Text helpers shared by the input layer and the business modules.
// All text in HMS is UTF-8 (SRS v11 draft section 2.4, portability rules).

#include <cstddef>
#include <string>
#include <string_view>

namespace hms::text {

bool isValidUtf8(std::string_view s);

// Number of Unicode code points ("characters" in the SRS limits).
// Only meaningful for valid UTF-8.
std::size_t utf8Length(std::string_view s);

// True if s contains a C0 control character (U+0000-U+001F), DEL (U+007F)
// or a C1 control character (U+0080-U+009F). SRS 3.1: rejected at every prompt.
bool containsControlChars(std::string_view s);

std::string trimSpaces(std::string_view s);     // surrounding ASCII spaces
std::string collapseSpaces(std::string_view s); // runs of spaces -> one space
std::string asciiLower(std::string_view s);     // A-Z -> a-z, other bytes unchanged

// Name normalisation for duplicate checks and name search (SRS HMS-F-002
// comments): trimmed, repeated spaces collapsed, compared without regard to
// letter case. Case folding is ASCII-only; scripts without case are unchanged.
std::string normaliseName(std::string_view s);

// Pads (or cuts, adding "...") to a display width counted in code points.
std::string fitWidth(std::string_view s, std::size_t width);

}  // namespace hms::text
