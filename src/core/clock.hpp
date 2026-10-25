#pragma once
// Time handling (SRS v11 draft section 2.5 "Time"): all code reads "now" through
// one replaceable Clock so that tests can fix the time. Times the system
// records itself are UTC; calendar rules (date of birth, "today") use the
// hospital-local date, computed with a fixed UTC offset (Asia/Kolkata has no
// daylight-saving time, so a fixed +05:30 offset is exact).

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace hms {

using UtcSeconds = std::chrono::time_point<std::chrono::system_clock, std::chrono::seconds>;

struct Date {
    int year = 1970;
    int month = 1;
    int day = 1;

    static bool isValid(int year, int month, int day);
    static std::optional<Date> fromIso(std::string_view iso);  // "YYYY-MM-DD"
    std::string toIso() const;                                  // storage format
    std::string toDisplay() const;                              // "DD-MM-YYYY" (SRS 3.1)

    friend bool operator==(const Date& a, const Date& b) {
        return a.year == b.year && a.month == b.month && a.day == b.day;
    }
    friend bool operator!=(const Date& a, const Date& b) { return !(a == b); }
    friend bool operator<(const Date& a, const Date& b) {
        if (a.year != b.year) return a.year < b.year;
        if (a.month != b.month) return a.month < b.month;
        return a.day < b.day;
    }
    friend bool operator>(const Date& a, const Date& b) { return b < a; }
    friend bool operator<=(const Date& a, const Date& b) { return !(b < a); }
    friend bool operator>=(const Date& a, const Date& b) { return !(a < b); }
};

// Whole years between dob and today (0 if today is before the first birthday).
int ageInYears(const Date& dob, const Date& today);

// Calendar arithmetic (proleptic Gregorian), independent of the C library.
std::int64_t daysFromCivil(int year, int month, int day);
Date civilFromDays(std::int64_t days);

class Clock {
public:
    virtual ~Clock() = default;
    virtual UtcSeconds now() const = 0;
};

class SystemClock final : public Clock {
public:
    UtcSeconds now() const override;
};

// Test clock: returns a fixed instant until it is changed.
class FixedClock final : public Clock {
public:
    explicit FixedClock(UtcSeconds t) : t_(t) {}
    UtcSeconds now() const override { return t_; }
    void set(UtcSeconds t) { t_ = t; }
    void advance(std::chrono::seconds s) { t_ += s; }

private:
    UtcSeconds t_;
};

UtcSeconds makeUtc(int year, int month, int day, int hour = 0, int minute = 0, int second = 0);
std::string toIsoUtc(UtcSeconds t);                         // "2026-10-25T07:54:00Z"
Date localDate(UtcSeconds t, int utcOffsetMinutes);        // hospital-local calendar date

}  // namespace hms
