#include "appointment/appointment.hpp"

#include <cstdio>

#include "core/text.hpp"

namespace hms {

namespace {
bool allDigits(std::string_view s) {
    if (s.empty()) return false;
    for (char c : s)
        if (c < '0' || c > '9') return false;
    return true;
}
int toInt(std::string_view s) {
    int v = 0;
    for (char c : s) v = v * 10 + (c - '0');
    return v;
}
}  // namespace

std::string minuteText(int minute) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%02d:%02d", minute / 60, minute % 60);
    return buf;
}

std::string LocalSlot::toKey() const { return date.toIso() + "T" + minuteText(minute); }
std::string LocalSlot::toDisplay() const { return date.toDisplay() + " " + minuteText(minute); }
std::string LocalSlot::timeText() const { return minuteText(minute); }

std::int64_t LocalSlot::epochMinutes() const {
    return daysFromCivil(date.year, date.month, date.day) * 1440 + minute;
}

std::optional<LocalSlot> LocalSlot::fromKey(std::string_view key) {
    if (key.size() != 16 || key[10] != 'T' || key[13] != ':') return std::nullopt;
    auto d = Date::fromIso(key.substr(0, 10));
    const auto h = key.substr(11, 2), m = key.substr(14, 2);
    if (!d || !allDigits(h) || !allDigits(m)) return std::nullopt;
    const int hour = toInt(h), minute = toInt(m);
    if (hour > 23 || minute > 59) return std::nullopt;
    return LocalSlot{*d, hour * 60 + minute};
}

LocalSlot localSlotAt(UtcSeconds t, int utcOffsetMinutes) {
    std::int64_t secs = t.time_since_epoch().count() + static_cast<std::int64_t>(utcOffsetMinutes) * 60;
    std::int64_t days = secs / 86400;
    std::int64_t rem = secs % 86400;
    if (rem < 0) {
        rem += 86400;
        --days;
    }
    return LocalSlot{civilFromDays(days), static_cast<int>(rem / 60)};
}

int isoWeekday(const Date& d) {
    // 1970-01-01 was a Thursday (ISO weekday 4).
    const std::int64_t days = daysFromCivil(d.year, d.month, d.day);
    std::int64_t w = (days + 3) % 7;
    if (w < 0) w += 7;
    return static_cast<int>(w) + 1;
}

std::string weekdayName(int isoWeekday) {
    static const char* kNames[7] = {"Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday", "Sunday"};
    if (isoWeekday < 1 || isoWeekday > 7) return "?";
    return kNames[isoWeekday - 1];
}

std::string formatAppointmentId(std::int64_t id) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "APT-%06lld", static_cast<long long>(id));
    return buf;
}

std::optional<std::int64_t> parseAppointmentId(std::string_view textIn) {
    const std::string s = text::trimSpaces(textIn);
    std::string_view v = s;
    if (v.size() >= 3 && text::asciiLower(v.substr(0, 3)) == "apt") {
        v.remove_prefix(3);
        if (!v.empty() && v.front() == '-') v.remove_prefix(1);
    }
    if (v.empty() || v.size() > 18 || !allDigits(v)) return std::nullopt;
    std::int64_t value = 0;
    for (char c : v) value = value * 10 + (c - '0');
    if (value <= 0) return std::nullopt;
    return value;
}

std::optional<WeeklyHours> DoctorSummary::hoursOn(int weekday) const {
    for (const auto& h : hours)
        if (h.weekday == weekday) return h;
    return std::nullopt;
}

std::string to_string(AppointmentStatus s) {
    switch (s) {
        case AppointmentStatus::Booked: return "Booked";
        case AppointmentStatus::Cancelled: return "Cancelled";
        case AppointmentStatus::Completed: return "Completed";
        case AppointmentStatus::NoShow: return "No-show";
    }
    return "?";
}

std::string storageName(AppointmentStatus s) {
    switch (s) {
        case AppointmentStatus::Booked: return "BOOKED";
        case AppointmentStatus::Cancelled: return "CANCELLED";
        case AppointmentStatus::Completed: return "COMPLETED";
        case AppointmentStatus::NoShow: return "NO_SHOW";
    }
    return "BOOKED";
}

std::optional<AppointmentStatus> statusFromStorage(std::string_view s) {
    if (s == "BOOKED") return AppointmentStatus::Booked;
    if (s == "CANCELLED") return AppointmentStatus::Cancelled;
    if (s == "COMPLETED") return AppointmentStatus::Completed;
    if (s == "NO_SHOW") return AppointmentStatus::NoShow;
    return std::nullopt;
}

}  // namespace hms
