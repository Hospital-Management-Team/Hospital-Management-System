#include "core/clock.hpp"

#include <cstdio>

namespace hms {

namespace {
bool isLeap(int y) { return (y % 4 == 0 && y % 100 != 0) || (y % 400 == 0); }

int daysInMonth(int y, int m) {
    static const int kDays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (m == 2 && isLeap(y)) return 29;
    return kDays[m - 1];
}

bool allDigits(std::string_view s) {
    for (char c : s)
        if (c < '0' || c > '9') return false;
    return !s.empty();
}

int toInt(std::string_view s) {
    int v = 0;
    for (char c : s) v = v * 10 + (c - '0');
    return v;
}
}  // namespace

bool Date::isValid(int year, int month, int day) {
    if (year < 1 || year > 9999) return false;
    if (month < 1 || month > 12) return false;
    return day >= 1 && day <= daysInMonth(year, month);
}

std::optional<Date> Date::fromIso(std::string_view iso) {
    if (iso.size() != 10 || iso[4] != '-' || iso[7] != '-') return std::nullopt;
    auto y = iso.substr(0, 4), m = iso.substr(5, 2), d = iso.substr(8, 2);
    if (!allDigits(y) || !allDigits(m) || !allDigits(d)) return std::nullopt;
    Date out{toInt(y), toInt(m), toInt(d)};
    if (!isValid(out.year, out.month, out.day)) return std::nullopt;
    return out;
}

std::string Date::toIso() const {
    char buf[16];
    std::snprintf(buf, sizeof buf, "%04d-%02d-%02d", year, month, day);
    return buf;
}

std::string Date::toDisplay() const {
    char buf[16];
    std::snprintf(buf, sizeof buf, "%02d-%02d-%04d", day, month, year);
    return buf;
}

int ageInYears(const Date& dob, const Date& today) {
    if (today < dob) return 0;
    int age = today.year - dob.year;
    if (today.month < dob.month || (today.month == dob.month && today.day < dob.day)) --age;
    return age < 0 ? 0 : age;
}

// Howard Hinnant's days_from_civil / civil_from_days (public domain).
std::int64_t daysFromCivil(int year, int month, int day) {
    const std::int64_t y = static_cast<std::int64_t>(year) - (month <= 2 ? 1 : 0);
    const std::int64_t era = (y >= 0 ? y : y - 399) / 400;
    const std::int64_t yoe = y - era * 400;
    const std::int64_t mp = (month + 9) % 12;
    const std::int64_t doy = (153 * mp + 2) / 5 + day - 1;
    const std::int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + doe - 719468;
}

Date civilFromDays(std::int64_t z) {
    z += 719468;
    const std::int64_t era = (z >= 0 ? z : z - 146096) / 146097;
    const std::int64_t doe = z - era * 146097;
    const std::int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const std::int64_t y = yoe + era * 400;
    const std::int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const std::int64_t mp = (5 * doy + 2) / 153;
    const std::int64_t d = doy - (153 * mp + 2) / 5 + 1;
    const std::int64_t m = mp < 10 ? mp + 3 : mp - 9;
    return Date{static_cast<int>(y + (m <= 2 ? 1 : 0)), static_cast<int>(m), static_cast<int>(d)};
}

UtcSeconds SystemClock::now() const {
    return std::chrono::time_point_cast<std::chrono::seconds>(std::chrono::system_clock::now());
}

UtcSeconds makeUtc(int year, int month, int day, int hour, int minute, int second) {
    const std::int64_t days = daysFromCivil(year, month, day);
    const std::int64_t secs = days * 86400 + hour * 3600 + minute * 60 + second;
    return UtcSeconds{std::chrono::seconds{secs}};
}

std::string toIsoUtc(UtcSeconds t) {
    const std::int64_t secs = t.time_since_epoch().count();
    std::int64_t days = secs / 86400;
    std::int64_t rem = secs % 86400;
    if (rem < 0) {
        rem += 86400;
        --days;
    }
    const Date d = civilFromDays(days);
    char buf[32];
    std::snprintf(buf, sizeof buf, "%04d-%02d-%02dT%02d:%02d:%02dZ", d.year, d.month, d.day,
                  static_cast<int>(rem / 3600), static_cast<int>((rem % 3600) / 60),
                  static_cast<int>(rem % 60));
    return buf;
}

Date localDate(UtcSeconds t, int utcOffsetMinutes) {
    std::int64_t secs = t.time_since_epoch().count() + static_cast<std::int64_t>(utcOffsetMinutes) * 60;
    std::int64_t days = secs / 86400;
    if (secs % 86400 < 0) --days;
    return civilFromDays(days);
}

}  // namespace hms
