#include "validation/input.hpp"

#include "core/text.hpp"

namespace hms::input {

namespace {
bool isDigit(char c) { return c >= '0' && c <= '9'; }

bool allDigits(std::string_view s) {
    if (s.empty()) return false;
    for (char c : s)
        if (!isDigit(c)) return false;
    return true;
}

// Parses up to 18 digits without overflow; caller checks allDigits first.
std::int64_t toInt64(std::string_view s) {
    std::int64_t v = 0;
    for (char c : s) v = v * 10 + (c - '0');
    return v;
}

std::string withCommas(std::int64_t v) {
    std::string s = std::to_string(v);
    for (int i = static_cast<int>(s.size()) - 3; i > 0; i -= 3) s.insert(static_cast<std::size_t>(i), ",");
    return s;
}
}  // namespace

Result<std::string> cleanLine(std::string_view raw, const std::string& field) {
    if (!text::isValidUtf8(raw)) {
        return Error::validation(field, "contains characters that are not valid text (UTF-8)");
    }
    if (text::utf8Length(raw) > kMaxLineChars) {
        return Error::validation(field, "is longer than the maximum of 1,000 characters");
    }
    std::string trimmed = text::trimSpaces(raw);
    if (text::containsControlChars(trimmed)) {
        return Error::validation(field, "contains control characters (such as tabs); type plain text only");
    }
    return trimmed;
}

Result<std::string> requireLine(std::string_view raw, const std::string& field) {
    auto r = cleanLine(raw, field);
    if (!r) return r;
    if (r.value().empty()) return Error::validation(field, "is required");
    return r;
}

Result<Date> parseDate(std::string_view text, const std::string& field) {
    const std::string kFormat = "must be a date in the form DD-MM-YYYY, for example 05-03-1990";
    const auto first = text.find('-');
    if (first == std::string_view::npos) return Error::validation(field, kFormat);
    const auto second = text.find('-', first + 1);
    if (second == std::string_view::npos || text.find('-', second + 1) != std::string_view::npos) {
        return Error::validation(field, kFormat);
    }
    const auto d = text.substr(0, first);
    const auto m = text.substr(first + 1, second - first - 1);
    const auto y = text.substr(second + 1);
    if (!allDigits(d) || !allDigits(m) || !allDigits(y) || d.size() > 2 || m.size() > 2 || y.size() != 4) {
        return Error::validation(field, kFormat);
    }
    const int day = static_cast<int>(toInt64(d));
    const int month = static_cast<int>(toInt64(m));
    const int year = static_cast<int>(toInt64(y));
    if (!Date::isValid(year, month, day)) {
        return Error::validation(field, std::string(text) + " is not a real calendar date");
    }
    return Date{year, month, day};
}

Result<std::int64_t> parseWholeNumber(std::string_view text, const std::string& field, std::int64_t min,
                                      std::int64_t max) {
    if (!allDigits(text)) return Error::validation(field, "must be a whole number (digits only)");
    if (text.size() > 18) return Error::validation(field, "must be at most " + withCommas(max));
    const std::int64_t v = toInt64(text);
    if (v < min || v > max) {
        return Error::validation(field, "must be between " + withCommas(min) + " and " + withCommas(max));
    }
    return v;
}

Result<std::int64_t> parseMoneyPaise(std::string_view text, const std::string& field, bool mustBePositive,
                                     std::int64_t maxPaise) {
    const std::string kFormat = "must be an amount such as 10, 10.5 or 10.50 (rupees, up to two decimals)";
    if (text.empty()) return Error::validation(field, kFormat);
    const auto dot = text.find('.');
    std::string_view rupees = text, paise;
    if (dot != std::string_view::npos) {
        rupees = text.substr(0, dot);
        paise = text.substr(dot + 1);
        if (paise.empty() || paise.size() > 2 || !allDigits(paise)) return Error::validation(field, kFormat);
    }
    if (!allDigits(rupees)) return Error::validation(field, kFormat);
    if (rupees.size() > 15) return Error::validation(field, "is too large");
    std::int64_t value = toInt64(rupees) * 100;
    if (!paise.empty()) value += paise.size() == 1 ? toInt64(paise) * 10 : toInt64(paise);
    if (mustBePositive && value == 0) return Error::validation(field, "must be greater than zero");
    if (value > maxPaise) {
        return Error::validation(field, "must not exceed " + withCommas(maxPaise / 100) + " rupees");
    }
    return value;
}

}  // namespace hms::input
