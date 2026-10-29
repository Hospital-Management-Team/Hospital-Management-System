#pragma once
// Appointment Module - data types (SRS HMS-F-010..F-013, F-020, F-021; issue #4).
//
// Definitions the submitted SRS leaves open (docs/deviation-log.md):
//  - A *slot* is a fixed-length period (AppointmentPolicy::slotMinutes) cut
//    from a doctor's working hours for that weekday, starting at the start of
//    those hours.
//  - Two appointments *overlap* when their periods intersect. A doctor, and a
//    patient, can hold at most one Booked appointment at any moment.
//  - Statuses: Booked, Cancelled, Completed, NoShow. This issue creates
//    Booked and Cancelled; Completed and NoShow are set by the consultation
//    feature. A rescheduled appointment is Cancelled with `replacedBy` set.
//  - All appointment times are hospital-local (Asia/Kolkata, UTC+05:30).

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/clock.hpp"

namespace hms {

// A hospital-local date and time of day (minutes after midnight).
struct LocalSlot {
    Date date;
    int minute = 0;  // 0-1439

    std::string toKey() const;      // storage form "YYYY-MM-DDTHH:MM" (sorts in time order)
    std::string toDisplay() const;  // "DD-MM-YYYY HH:MM"
    std::string timeText() const;   // "HH:MM"
    std::int64_t epochMinutes() const;  // minutes since 1970-01-01 00:00 local
    static std::optional<LocalSlot> fromKey(std::string_view key);

    friend bool operator==(const LocalSlot& a, const LocalSlot& b) {
        return a.date == b.date && a.minute == b.minute;
    }
    friend bool operator!=(const LocalSlot& a, const LocalSlot& b) { return !(a == b); }
    friend bool operator<(const LocalSlot& a, const LocalSlot& b) {
        return a.date != b.date ? a.date < b.date : a.minute < b.minute;
    }
};

// Hospital-local date and time for a UTC instant (rounded down to the minute).
LocalSlot localSlotAt(UtcSeconds t, int utcOffsetMinutes);

// ISO weekday: 1 = Monday ... 7 = Sunday.
int isoWeekday(const Date& d);
std::string weekdayName(int isoWeekday);  // "Monday"
std::string minuteText(int minute);       // "09:30"

// Appointment numbers are shown as APT- plus at least six digits.
std::string formatAppointmentId(std::int64_t id);
std::optional<std::int64_t> parseAppointmentId(std::string_view text);  // "APT-000012", "apt-12", "12"

// One working window on one weekday.
struct WeeklyHours {
    int weekday = 1;       // 1 = Monday ... 7 = Sunday
    int startMinute = 0;   // inclusive
    int endMinute = 0;     // exclusive; a slot must end by this time
};

// What the Admin enters for a doctor (HMS-F-020).
struct DoctorScheduleDraft {
    std::string department;
    std::string specialization;  // may be empty
    std::vector<WeeklyHours> hours;  // at most one window per weekday
};

struct DoctorSummary {
    std::int64_t staffId = 0;
    std::string username;
    bool hasProfile = false;
    std::string department;
    std::optional<std::string> specialization;
    std::vector<WeeklyHours> hours;  // ordered by weekday

    // The working window for a weekday, if any.
    std::optional<WeeklyHours> hoursOn(int weekday) const;
};

// Result of saving a schedule.
struct ScheduleSaved {
    // Booked future appointments that now fall outside the doctor's hours.
    // They are kept; reception should reschedule them.
    std::int64_t bookedOutsideHours = 0;
};

// A doctor who works on a given date, with that day's free slots.
struct DoctorDay {
    DoctorSummary doctor;
    WeeklyHours hours;
    std::vector<LocalSlot> freeSlots;
};

enum class AppointmentStatus { Booked, Cancelled, Completed, NoShow };
std::string to_string(AppointmentStatus s);         // "Booked"
std::string storageName(AppointmentStatus s);       // "BOOKED"
std::optional<AppointmentStatus> statusFromStorage(std::string_view s);

struct AppointmentRecord {
    std::int64_t id = 0;
    std::int64_t uhid = 0;
    std::string patientName;
    std::int64_t doctorStaffId = 0;
    std::string doctorUsername;
    std::string department;
    LocalSlot start;
    int slotMinutes = 0;
    AppointmentStatus status = AppointmentStatus::Booked;
    std::optional<std::int64_t> replacedBy;
};

struct ReminderItem {
    AppointmentRecord appointment;
    bool newlyGenerated = false;  // the reminder entry was created by this run
};

}  // namespace hms
