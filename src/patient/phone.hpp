#pragma once
// Phone number canonical form (SRS section 1.4, "Phone number").
//
// After removing spaces, hyphens, dots and brackets, an entry is accepted
// when it is 10 digits, 0 + 10 digits, 91 + 10 digits or +91 + 10 digits,
// and the 10 digits start with 6, 7, 8 or 9 (Indian mobile). The canonical
// form is the 10 digits. The same rule is used by registration, duplicate
// detection, update and search.

#include <string>
#include <string_view>

#include "core/result.hpp"

namespace hms::phone {

inline constexpr const char* kField = "Mobile number";

// Returns the 10-digit canonical form or a Validation error naming the field.
// The caller decides whether an empty entry is allowed.
Result<std::string> canonicalise(std::string_view raw);

// "+91 98765 43210"
std::string display(const std::string& canonical);

}  // namespace hms::phone
