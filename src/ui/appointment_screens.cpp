// Appointment screens of ConsoleApp (issue #4): reception books, reschedules,
// cancels and lists reminders; a doctor sees their own queue. Like the rest of
// ConsoleApp, these screens hold no business rule: they read input, call one
// AppointmentService function and show the result.

#include <ostream>

#include "core/text.hpp"
#include "patient/phone.hpp"
#include "patient/uhid.hpp"
#include "ui/console_app.hpp"
#include "validation/input.hpp"

namespace hms {

namespace {
const char* kCancelled = "Cancelled. Nothing was changed.\n";

std::string mobileCell(const PatientRecord& p) {
    if (p.phone) return phone::display(*p.phone);
    return p.noPhoneConfirmed ? "No phone" : "Not recorded";
}

std::string dayText(const Date& d) {
    static const char* kShort[7] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
    return std::string(kShort[isoWeekday(d) - 1]) + " " + d.toDisplay();
}
}  // namespace

bool ConsoleApp::blockedForChanges() {
    if (!changesBlocked_) return false;
    io_.out() << "  ! Changes are blocked until the application is restarted (an earlier save could not be "
                 "confirmed).\n";
    return true;
}

std::optional<PatientRecord> ConsoleApp::askPatient() {
    auto& out = io_.out();
    while (true) {
        auto line = io_.readLine("Patient UHID (find it first with option 2 or 4 if needed): ");
        if (!line || text::trimSpaces(*line).empty()) return std::nullopt;
        auto id = parseUhid(*line);
        if (!id) {
            out << "  ! UHID: must look like HMS-000123 or 123\n";
            continue;
        }
        auto r = patients_.getPatient(session_, *id);
        if (!r) {
            handleFailure(r.error());
            if (r.error().kind == ErrorKind::NotFound) continue;
            return std::nullopt;
        }
        out << "  Patient: " << r.value().fullName << " (" << formatUhid(r.value().uhid) << "), born "
            << dobText(r.value()) << "\n";
        return r.value();
    }
}

std::optional<Date> ConsoleApp::askDate(const std::string& prompt, std::optional<Date> enterMeans) {
    while (true) {
        auto line = io_.readLine(prompt);
        if (!line) return std::nullopt;
        const std::string v = text::trimSpaces(*line);
        if (v.empty()) {
            if (enterMeans) return enterMeans;
            return std::nullopt;
        }
        auto d = input::parseDate(v, "Date");
        if (d) return d.value();
        io_.showError(d.error());
    }
}

std::optional<LocalSlot> ConsoleApp::pickSlot(const std::vector<LocalSlot>& slots) {
    auto& out = io_.out();
    out << "  Free slots (" << appointments_.policy().appointment.slotMinutes << " minutes each):\n";
    for (std::size_t i = 0; i < slots.size(); ++i) {
        out << "  " << text::fitWidth(std::to_string(i + 1) + ".", 4) << slots[i].timeText()
            << ((i + 1) % 6 == 0 || i + 1 == slots.size() ? "\n" : "   ");
    }
    auto n = io_.ask<std::int64_t>("Slot number: ", [&](const std::string& s) {
        return input::parseWholeNumber(text::trimSpaces(s), "Slot number", 1, static_cast<std::int64_t>(slots.size()));
    });
    if (!n) return std::nullopt;
    return slots[static_cast<std::size_t>(*n - 1)];
}

// ------------------------------------------------------------ book (HMS-F-010, F-011)

void ConsoleApp::bookAppointmentScreen() {
    auto& out = io_.out();
    if (blockedForChanges()) return;
    out << "\nBOOK AN APPOINTMENT\n"
        << "To go back, press Enter at the first prompt, or Ctrl+D (Linux) / Ctrl+Z then Enter (Windows).\n";
    auto patient = askPatient();
    if (!patient) return;

    while (true) {
        auto date = askDate("Date (DD-MM-YYYY), or press Enter for today: ", appointments_.today());
        if (!date) { out << kCancelled; return; }
        auto days = appointments_.doctorsOn(session_, *date);
        if (!days) {
            handleFailure(days.error());
            if (days.error().kind == ErrorKind::Validation) continue;
            return;
        }
        if (days.value().empty()) {
            out << "  No doctor works on " << dayText(*date) << ". Choose another date.\n";
            continue;
        }
        out << "\n  Doctors working on " << dayText(*date) << ":\n"
            << "  #   " << text::fitWidth("Doctor", 18) << text::fitWidth("Department", 22) << text::fitWidth("Hours", 13)
            << "Free slots\n";
        int i = 1;
        for (const auto& d : days.value()) {
            out << "  " << text::fitWidth(std::to_string(i++) + ".", 4) << text::fitWidth("Dr " + d.doctor.username, 18)
                << text::fitWidth(d.doctor.department, 22)
                << text::fitWidth(minuteText(d.hours.startMinute) + "-" + minuteText(d.hours.endMinute), 13)
                << d.freeSlots.size() << "\n";
        }
        auto pick = io_.ask<std::int64_t>("Doctor (row number): ", [&](const std::string& s) {
            return input::parseWholeNumber(text::trimSpaces(s), "Doctor", 1,
                                           static_cast<std::int64_t>(days.value().size()));
        });
        if (!pick) { out << kCancelled; return; }
        const DoctorDay& day = days.value()[static_cast<std::size_t>(*pick - 1)];
        if (day.freeSlots.empty()) {
            out << "  Dr " << day.doctor.username << " has no free slots left on " << dayText(*date)
                << ". Choose another date or doctor.\n";
            continue;
        }
        auto slot = pickSlot(day.freeSlots);
        if (!slot) { out << kCancelled; return; }

        out << "\n  Book " << patient->fullName << " (" << formatUhid(patient->uhid) << ") with Dr "
            << day.doctor.username << " (" << day.doctor.department << ") on " << dayText(slot->date) << " at "
            << slot->timeText() << "?\n";
        auto yes = io_.askYesNo("  Confirm (Y/N): ");
        if (!yes || !*yes) { out << kCancelled; return; }

        auto booked = appointments_.book(session_, patient->uhid, day.doctor.staffId, *slot);
        if (!booked) {
            handleFailure(booked.error());
            // The slot was taken meanwhile: show the doctors and free slots again.
            if (booked.error().kind == ErrorKind::Conflict) continue;
            return;
        }
        const auto& a = booked.value();
        out << "\n  Appointment booked: " << formatAppointmentId(a.id) << "\n"
            << "  " << dayText(a.start.date) << " at " << a.start.timeText() << ", Dr " << a.doctorUsername << " ("
            << a.department << ")\n"
            << "  Give the patient this appointment number.\n";
        return;
    }
}

// ------------------------------------------------------------ reschedule / cancel (HMS-F-012)

void ConsoleApp::changeAppointmentScreen() {
    auto& out = io_.out();
    if (blockedForChanges()) return;
    out << "\nRESCHEDULE OR CANCEL AN APPOINTMENT\n"
        << "  Changes close " << appointments_.policy().appointment.cutoffMinutes
        << " minutes before the appointment starts.\n";
    auto patient = askPatient();
    if (!patient) return;
    auto list = appointments_.upcomingForPatient(session_, patient->uhid);
    if (!list) { handleFailure(list.error()); return; }
    if (list.value().empty()) {
        out << "  This patient has no upcoming appointments.\n";
        return;
    }
    out << "  #   " << text::fitWidth("Appointment", 13) << text::fitWidth("When", 22) << text::fitWidth("Doctor", 18)
        << "Department\n";
    int i = 1;
    for (const auto& a : list.value()) {
        out << "  " << text::fitWidth(std::to_string(i++) + ".", 4) << text::fitWidth(formatAppointmentId(a.id), 13)
            << text::fitWidth(dayText(a.start.date) + " " + a.start.timeText(), 22)
            << text::fitWidth("Dr " + a.doctorUsername, 18) << a.department << "\n";
    }
    auto pick = io_.ask<std::int64_t>("Appointment (row number): ", [&](const std::string& s) {
        return input::parseWholeNumber(text::trimSpaces(s), "Appointment", 1,
                                       static_cast<std::int64_t>(list.value().size()));
    });
    if (!pick) { out << kCancelled; return; }
    const AppointmentRecord a = list.value()[static_cast<std::size_t>(*pick - 1)];

    auto action = io_.ask<std::string>("R = reschedule, C = cancel the appointment: ",
                                       [](const std::string& s) -> Result<std::string> {
                                           const std::string v = text::asciiLower(text::trimSpaces(s));
                                           if (v == "r" || v == "c") return v;
                                           return Error::validation("Choice", "must be R or C");
                                       });
    if (!action) { out << kCancelled; return; }

    if (*action == "c") {
        out << "  Cancel " << formatAppointmentId(a.id) << " (" << dayText(a.start.date) << " at "
            << a.start.timeText() << ", Dr " << a.doctorUsername << ")?\n";
        auto yes = io_.askYesNo("  Confirm (Y/N): ");
        if (!yes || !*yes) { out << kCancelled; return; }
        auto r = appointments_.cancel(session_, a.id);
        if (!r) { handleFailure(r.error()); return; }
        out << "  " << formatAppointmentId(a.id) << " is cancelled. The slot is free for other patients.\n";
        return;
    }

    while (true) {
        auto date = askDate("New date (DD-MM-YYYY), or press Enter for the same day: ", a.start.date);
        if (!date) { out << kCancelled; return; }
        auto slots = appointments_.freeSlots(session_, a.doctorStaffId, *date);
        if (!slots) {
            handleFailure(slots.error());
            if (slots.error().kind == ErrorKind::Validation) continue;
            return;
        }
        if (slots.value().empty()) {
            out << "  Dr " << a.doctorUsername << " has no free slots on " << dayText(*date)
                << ". Choose another date.\n";
            continue;
        }
        auto slot = pickSlot(slots.value());
        if (!slot) { out << kCancelled; return; }
        out << "  Move " << formatAppointmentId(a.id) << " from " << dayText(a.start.date) << " " << a.start.timeText()
            << " to " << dayText(slot->date) << " " << slot->timeText() << " with Dr " << a.doctorUsername << "?\n";
        auto yes = io_.askYesNo("  Confirm (Y/N): ");
        if (!yes || !*yes) { out << kCancelled; return; }
        auto r = appointments_.reschedule(session_, a.id, *slot);
        if (!r) {
            handleFailure(r.error());
            if (r.error().kind != ErrorKind::Conflict && r.error().kind != ErrorKind::Validation) return;
            auto again = io_.askYesNo("  Try another slot? (Y/N): ");
            if (!again || !*again) { out << kCancelled; return; }
            continue;
        }
        out << "  Rescheduled. New appointment: " << formatAppointmentId(r.value().id) << ", "
            << dayText(r.value().start.date) << " at " << r.value().start.timeText() << ".\n"
            << "  " << formatAppointmentId(a.id) << " is cancelled and its slot is free again.\n";
        return;
    }
}

// ------------------------------------------------------------ reminders (HMS-F-013)

void ConsoleApp::remindersScreen() {
    auto& out = io_.out();
    if (blockedForChanges()) return;
    auto r = appointments_.remindersDue(session_);
    if (!r) { handleFailure(r.error()); return; }
    out << "\nAPPOINTMENT REMINDERS - booked appointments in the next "
        << appointments_.policy().appointment.reminderLeadHours << " hours\n";
    if (r.value().empty()) {
        out << "  No booked appointments in this period.\n";
        return;
    }
    out << "  " << text::fitWidth("When", 22) << text::fitWidth("Appointment", 13) << text::fitWidth("Patient", 24)
        << text::fitWidth("Mobile", 17) << text::fitWidth("Doctor", 16) << "Reminder\n";
    int fresh = 0;
    for (const auto& item : r.value()) {
        const auto& a = item.appointment;
        std::string mobile = "-";
        if (auto p = patients_.getPatient(session_, a.uhid)) mobile = mobileCell(p.value());
        out << "  " << text::fitWidth(dayText(a.start.date) + " " + a.start.timeText(), 22)
            << text::fitWidth(formatAppointmentId(a.id), 13)
            << text::fitWidth(a.patientName + " " + formatUhid(a.uhid), 24) << text::fitWidth(mobile, 17)
            << text::fitWidth("Dr " + a.doctorUsername, 16) << (item.newlyGenerated ? "NEW" : "listed before") << "\n";
        if (item.newlyGenerated) ++fresh;
    }
    out << "  " << fresh << " new reminder" << (fresh == 1 ? "" : "s")
        << " recorded. Contact the patients marked NEW.\n";
}

// ------------------------------------------------------------ doctor's queue (HMS-F-021)

void ConsoleApp::myQueueScreen() {
    auto& out = io_.out();
    auto date = askDate("Date (DD-MM-YYYY), or press Enter for today: ", appointments_.today());
    if (!date) return;
    auto r = appointments_.myQueue(session_, *date);
    if (!r) { handleFailure(r.error()); return; }
    out << "\nMY APPOINTMENTS - " << session_.displayName << " - " << dayText(*date) << "\n";
    if (r.value().empty()) {
        out << "  No appointments.\n";
        return;
    }
    out << "  " << text::fitWidth("Time", 7) << text::fitWidth("Appointment", 13) << text::fitWidth("UHID", 12)
        << text::fitWidth("Patient", 28) << "Status\n";
    for (const auto& a : r.value()) {
        out << "  " << text::fitWidth(a.start.timeText(), 7) << text::fitWidth(formatAppointmentId(a.id), 13)
            << text::fitWidth(formatUhid(a.uhid), 12) << text::fitWidth(a.patientName, 28) << to_string(a.status)
            << "\n";
    }
    out << "  " << r.value().size() << " appointment" << (r.value().size() == 1 ? "" : "s") << ".\n";
}

}  // namespace hms
