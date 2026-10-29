#include "ui/doctor_schedule_screen.hpp"

#include <ostream>

#include "core/text.hpp"
#include "validation/input.hpp"

namespace hms {

namespace {

std::string hoursSummary(const DoctorSummary& d) {
    if (d.hours.empty()) return "no working hours";
    std::string s;
    for (const auto& h : d.hours) {
        if (!s.empty()) s += ", ";
        s += weekdayName(h.weekday).substr(0, 3) + " " + minuteText(h.startMinute) + "-" + minuteText(h.endMinute);
    }
    return s;
}

// "09:00-13:00" -> window on `weekday`.
Result<WeeklyHours> parseWindow(const std::string& text, int weekday) {
    const std::string field = "Hours on " + weekdayName(weekday);
    const auto dash = text.find('-');
    if (dash == std::string::npos) {
        return Error::validation(field, "must be two times joined by '-', for example 09:00-13:00");
    }
    auto start = input::parseTimeOfDay(text::trimSpaces(text.substr(0, dash)), field);
    if (!start) return start.error();
    auto end = input::parseTimeOfDay(text::trimSpaces(text.substr(dash + 1)), field);
    if (!end) return end.error();
    if (end.value() <= start.value()) return Error::validation(field, "the end time must be after the start time");
    return WeeklyHours{weekday, start.value(), end.value()};
}


// Asks for the hours of each weekday; Enter keeps the current value. Returns
// nullopt if input ends.
std::optional<std::vector<WeeklyHours>> askWeeklyHours(ConsoleIO& io, const DoctorSummary& current) {
    auto& out = io.out();
    std::vector<WeeklyHours> hours;
    out << "Weekly hours: type HH:MM-HH:MM (for example 09:00-13:00), 'off' if not working that day,\n"
           "or press Enter to keep the value in [brackets].\n";
    for (int day = 1; day <= 7; ++day) {
        const auto cur = current.hoursOn(day);
        const std::string curText = cur ? minuteText(cur->startMinute) + "-" + minuteText(cur->endMinute) : "off";
        while (true) {
            auto line = io.readLine("  " + text::fitWidth(weekdayName(day), 10) + "[" + curText + "]: ");
            if (!line) return std::nullopt;
            const std::string v = text::asciiLower(text::trimSpaces(*line));
            if (v.empty()) {
                if (cur) hours.push_back(*cur);
                break;
            }
            if (v == "off") break;
            auto w = parseWindow(v, day);
            if (!w) { io.showError(w.error()); continue; }
            hours.push_back(w.value());
            break;
        }
    }
    return hours;
}

}  // namespace

void doctorScheduleScreen(ConsoleIO& io, AppointmentService& appointments, const Session& session) {
    auto& out = io.out();
    auto doctors = appointments.listDoctors(session);
    if (!doctors) { io.showError(doctors.error()); return; }
    out << "\nDOCTOR SCHEDULES\n";
    if (doctors.value().empty()) {
        out << "  There are no Doctor accounts yet. Create one with option 1 first.\n";
        return;
    }
    out << "  #   " << text::fitWidth("Doctor", 18) << text::fitWidth("Department", 22) << "Weekly hours\n";
    int i = 1;
    for (const auto& d : doctors.value()) {
        out << "  " << text::fitWidth(std::to_string(i++) + ".", 4) << text::fitWidth("Dr " + d.username, 18)
            << text::fitWidth(d.hasProfile ? d.department : "(not set)", 22) << hoursSummary(d) << "\n";
    }
    auto pick = io.ask<std::int64_t>("Doctor to change (row number): ", [&](const std::string& s) {
        return input::parseWholeNumber(text::trimSpaces(s), "Doctor", 1,
                                       static_cast<std::int64_t>(doctors.value().size()));
    });
    if (!pick) return;
    const DoctorSummary current = doctors.value()[static_cast<std::size_t>(*pick - 1)];

    out << "\nDr " << current.username << ". Press Enter to keep the value in [brackets].\n";
    DoctorScheduleDraft draft;
    while (true) {
        auto dept = io.readLine("Department" + (current.hasProfile ? " [" + current.department + "]" : "") + ": ");
        if (!dept) return;
        std::string v = text::trimSpaces(*dept);
        if (v.empty() && current.hasProfile) v = current.department;
        if (v.empty()) { out << "  ! Department: is required\n"; continue; }
        draft.department = v;
        break;
    }
    {
        const std::string cur = current.specialization.value_or("");
        auto spec = io.readLine("Specialization (optional, '-' to clear)" + (cur.empty() ? "" : " [" + cur + "]") + ": ");
        if (!spec) return;
        std::string v = text::trimSpaces(*spec);
        if (v.empty()) v = cur;
        if (v == "-") v.clear();
        draft.specialization = v;
    }

    auto hours = askWeeklyHours(io, current);
    if (!hours) return;
    draft.hours = *hours;

    DoctorSummary preview = current;
    preview.hours = draft.hours;
    out << "\n  Dr " << current.username << ": " << draft.department
        << (draft.specialization.empty() ? "" : " (" + draft.specialization + ")") << "\n"
        << "  " << hoursSummary(preview) << "\n";
    auto yes = io.askYesNo("  Save? (Y/N): ");
    if (!yes || !*yes) { out << "  Nothing was changed.\n"; return; }

    auto saved = appointments.setDoctorSchedule(session, current.staffId, draft);
    if (!saved) { io.showError(saved.error()); return; }
    out << "  Saved. The booking screen uses these hours from now on.\n";
    if (saved.value().bookedOutsideHours > 0) {
        out << "  ! " << saved.value().bookedOutsideHours
            << " booked appointment(s) now fall outside these hours. They were kept; ask reception to "
               "reschedule them.\n";
    }
}


void ownHoursScreen(ConsoleIO& io, AppointmentService& appointments, const Session& session) {
    auto& out = io.out();
    auto me = appointments.myProfile(session);
    if (!me) { io.showError(me.error()); return; }
    const DoctorSummary& current = me.value();
    out << "\nMY WEEKLY WORKING HOURS - Dr " << current.username << "\n";
    if (!current.hasProfile) {
        out << "  Your doctor profile has not been set up yet. Ask an Administrator to set your department first.\n";
        return;
    }
    out << "  Department: " << current.department << "\n  " << hoursSummary(current) << "\n"
        << "Change the days that differ. Reception books only inside these hours, and the Administrator can see the change.\n";
    auto hours = askWeeklyHours(io, current);
    if (!hours) return;
    DoctorSummary preview = current;
    preview.hours = *hours;
    out << "\n  " << hoursSummary(preview) << "\n";
    auto yes = io.askYesNo("  Save? (Y/N): ");
    if (!yes || !*yes) { out << "  Nothing was changed.\n"; return; }
    auto saved = appointments.setOwnHours(session, *hours);
    if (!saved) { io.showError(saved.error()); return; }
    out << "  Saved. The booking screen uses these hours from now on.\n";
    if (saved.value().bookedOutsideHours > 0) {
        out << "  ! " << saved.value().bookedOutsideHours
            << " booked appointment(s) now fall outside these hours. They were kept; ask reception to "
               "reschedule them.\n";
    }
}

}  // namespace hms
