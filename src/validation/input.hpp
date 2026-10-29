#pragma once
// Input layer (SRS v11 draft section 3.1, task #13).
//
// Parsing and validation are kept apart from the business rules and from the
// console, so they can be unit-tested without the menu (HMS-NF-004). Every
// function returns either the parsed value or a Validation error that names
// the field and the rule.

#include <cstdint>
#include <string>
#include <string_view>

#include "core/clock.hpp"
#include "core/result.hpp"

namespace hms::input {

inline constexpr std::size_t kMaxLineChars = 1000;

// Every prompt: rejects invalid UTF-8, lines longer than 1,000 characters
// and control characters; trims surrounding spaces. An empty result is
// allowed here - whether empty is an error depends on the field.
Result<std::string> cleanLine(std::string_view raw, const std::string& field);

// Same, and an empty answer is an error ("<field> is required").
Result<std::string> requireLine(std::string_view raw, const std::string& field);

// DD-MM-YYYY (day and month may have one or two digits). Impossible dates
// such as 31-02-2026 are rejected.
Result<Date> parseDate(std::string_view text, const std::string& field);

// Time of day HH:MM, 24-hour clock (the hour may have one or two digits:
// 9:00 and 09:00 are the same). Returns minutes after midnight, 0-1439.
Result<int> parseTimeOfDay(std::string_view text, const std::string& field);

// Digits only (no sign, no spaces inside), within [min, max].
Result<std::int64_t> parseWholeNumber(std::string_view text, const std::string& field, std::int64_t min,
                                      std::int64_t max);

// Money (SRS 3.1): digits with an optional decimal point followed by one or
// two digits. Returns whole paise. 10, 10.5, 10.50 are valid; 10.555, 10.,
// .50, -10, 1e3 and empty are not. If mustBePositive, zero is rejected.
Result<std::int64_t> parseMoneyPaise(std::string_view text, const std::string& field, bool mustBePositive,
                                     std::int64_t maxPaise);

}  // namespace hms::input
