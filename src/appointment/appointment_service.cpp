#include "appointment/appointment_service.hpp"

#include <sqlite3.h>

#include <algorithm>
#include <set>
#include <string>

#include "core/text.hpp"
#include "patient/uhid.hpp"
#include "validation/input.hpp"

namespace hms {

namespace {
const char* kStorageFailed = "The data store reported an error. Nothing was saved. Please try again.";
const char* kReadFailed = "The data store reported an error. Please try again.";
const char* kOutcomeUnknown =
    "The data store could not confirm whether the change was saved. Do not enter it again. "
    "Restart the application and check the patient's appointments.";

LocalSlot fromEpochMinutes(std::int64_t m) {
    std::int64_t days = m / 1440;
    std::int64_t rem = m % 1440;
    if (rem < 0) {
        rem += 1440;
        --days;
    }
    return LocalSlot{civilFromDays(days), static_cast<int>(rem)};
}

bool overlaps(std::int64_t aStart, std::int64_t aLen, std::int64_t bStart, std::int64_t bLen) {
    return aStart < bStart + bLen && bStart < aStart + aLen;
}

std::string doctorLabel(const std::string& username) { return "Dr " + username; }

std::string hoursText(const WeeklyHours& h) { return minuteText(h.startMinute) + "-" + minuteText(h.endMinute); }

std::string staffRecordId(std::int64_t staffId) { return "STAFF-" + std::to_string(staffId); }
}  // namespace

AppointmentService::AppointmentService(Database& db, const Clock& clock, AppPolicy policy)
    : db_(db), clock_(clock), policy_(policy), repo_(db), audit_(db, clock) {}

Date AppointmentService::today() const { return localDate(clock_.now(), policy_.utcOffsetMinutes); }
LocalSlot AppointmentService::now() const { return localSlotAt(clock_.now(), policy_.utcOffsetMinutes); }

bool AppointmentService::authorize(const Session& session, Permission permission) {
    if (isAllowed(session.role, permission)) return true;
    try {
        Transaction tx(db_);
        AuditEntryInput e;
        e.action = auditActionFor(permission);
        e.outcome = AuditOutcome::Denied;
        audit_.append(session.userId, e);
        tx.commit();
    } catch (const std::exception&) {
        // The denial stands even if it could not be recorded.
    }
    return false;
}

std::vector<int> AppointmentService::gridMinutes(const WeeklyHours& hours) const {
    std::vector<int> out;
    const int len = policy_.appointment.slotMinutes;
    for (int m = hours.startMinute; m + len <= hours.endMinute; m += len) out.push_back(m);
    return out;
}

// ------------------------------------------------------------ schedules

std::optional<Error> AppointmentService::validateSchedule(const DoctorScheduleDraft& draft) const {
    std::vector<FieldError> errors;
    const auto& pol = policy_.appointment;
    auto dept = input::requireLine(draft.department, "Department");
    if (!dept) {
        errors.insert(errors.end(), dept.error().fields.begin(), dept.error().fields.end());
    } else if (text::utf8Length(dept.value()) > pol.maxDepartmentChars) {
        errors.push_back({"Department", "must be at most " + std::to_string(pol.maxDepartmentChars) + " characters"});
    }
    auto spec = input::cleanLine(draft.specialization, "Specialization");
    if (!spec) {
        errors.insert(errors.end(), spec.error().fields.begin(), spec.error().fields.end());
    } else if (text::utf8Length(spec.value()) > pol.maxDepartmentChars) {
        errors.push_back(
            {"Specialization", "must be at most " + std::to_string(pol.maxDepartmentChars) + " characters"});
    }
    std::set<int> seen;
    for (const auto& h : draft.hours) {
        const std::string field = "Hours on " + weekdayName(h.weekday);
        if (h.weekday < 1 || h.weekday > 7) {
            errors.push_back({"Weekday", "must be 1 (Monday) to 7 (Sunday)"});
            continue;
        }
        if (!seen.insert(h.weekday).second) {
            errors.push_back({field, "can be given only once"});
            continue;
        }
        if (h.startMinute < 0 || h.endMinute > 1440 || h.startMinute >= h.endMinute) {
            errors.push_back({field, "the end time must be after the start time"});
        } else if (h.endMinute - h.startMinute < pol.slotMinutes) {
            errors.push_back({field, "must be at least " + std::to_string(pol.slotMinutes) +
                                         " minutes long (one appointment slot)"});
        }
    }
    if (!errors.empty()) return Error::validation(std::move(errors));
    return std::nullopt;
}

Result<std::vector<DoctorSummary>> AppointmentService::listDoctors(const Session& session) {
    if (!authorize(session, Permission::ListDoctors)) {
        return Error::accessDenied("Your role (" + to_string(session.role) + ") may not list doctors.");
    }
    try {
        return repo_.listDoctors();
    } catch (const DbError&) {
        return Error::storageFailure(kReadFailed);
    }
}

Result<ScheduleSaved> AppointmentService::setDoctorSchedule(const Session& session, std::int64_t doctorStaffId,
                                                            const DoctorScheduleDraft& draft) {
    if (!authorize(session, Permission::ManageDoctorSchedule)) {
        return Error::accessDenied("Your role (" + to_string(session.role) + ") may not change doctor schedules.");
    }
    return applySchedule(session, doctorStaffId, draft);
}

Result<DoctorSummary> AppointmentService::myProfile(const Session& session) {
    if (!authorize(session, Permission::ManageOwnHours)) {
        return Error::accessDenied("Your role (" + to_string(session.role) + ") has no working hours to change.");
    }
    try {
        auto staffId = repo_.doctorIdForUsername(session.userId);
        auto doctor = staffId ? repo_.findDoctor(*staffId) : std::nullopt;
        if (!doctor) return Error::notFound("No Doctor account was found for your login.");
        return *doctor;
    } catch (const DbError&) {
        return Error::storageFailure(kReadFailed);
    }
}

Result<ScheduleSaved> AppointmentService::setOwnHours(const Session& session, const std::vector<WeeklyHours>& hours) {
    if (!authorize(session, Permission::ManageOwnHours)) {
        return Error::accessDenied("Your role (" + to_string(session.role) + ") may not change working hours.");
    }
    DoctorScheduleDraft draft;
    std::int64_t staffId = 0;
    try {
        // The doctor is the logged-in account; no other doctor's hours can be named.
        auto id = repo_.doctorIdForUsername(session.userId);
        auto doctor = id ? repo_.findDoctor(*id) : std::nullopt;
        if (!doctor) return Error::notFound("No Doctor account was found for your login.");
        if (!doctor->hasProfile) {
            return Error::conflict("Your doctor profile has not been set up yet. Ask an Administrator to set your "
                                   "department first.");
        }
        staffId = *id;
        draft.department = doctor->department;
        draft.specialization = doctor->specialization.value_or("");
    } catch (const DbError&) {
        return Error::storageFailure(kReadFailed);
    }
    draft.hours = hours;
    return applySchedule(session, staffId, draft);
}

Result<ScheduleSaved> AppointmentService::applySchedule(const Session& session, std::int64_t doctorStaffId,
                                                        const DoctorScheduleDraft& draft) {
    if (auto e = validateSchedule(draft)) return *e;

    DoctorScheduleDraft clean = draft;
    clean.department = input::requireLine(draft.department, "Department").value();
    clean.specialization = input::cleanLine(draft.specialization, "Specialization").value();
    std::sort(clean.hours.begin(), clean.hours.end(),
              [](const WeeklyHours& a, const WeeklyHours& b) { return a.weekday < b.weekday; });

    try {
        Transaction tx(db_);
        if (!repo_.findDoctor(doctorStaffId)) {
            return Error::notFound("No Doctor account with staff number " + std::to_string(doctorStaffId) + ".");
        }
        repo_.saveSchedule(doctorStaffId, clean, toIsoUtc(clock_.now()));

        AuditEntryInput e;
        e.action = audit_action::kDoctorScheduleSet;
        e.recordId = staffRecordId(doctorStaffId);
        e.changedFields = {"department", "specialization", "weekly_hours"};
        audit_.append(session.userId, e);

        // Booked appointments that no longer fit the new hours are kept;
        // reception is told how many to reschedule.
        ScheduleSaved saved;
        const auto doctor = repo_.findDoctor(doctorStaffId);
        for (const auto& a : repo_.bookedForDoctorFrom(doctorStaffId, now().toKey())) {
            const auto h = doctor->hoursOn(isoWeekday(a.start.date));
            const bool fits = h && a.start.minute >= h->startMinute && a.start.minute + a.slotMinutes <= h->endMinute;
            if (!fits) ++saved.bookedOutsideHours;
        }
        db_.faultPoint("schedule_before_commit");
        tx.commit();
        return saved;
    } catch (const CommitUncertain&) {
        return Error::outcomeUnknown(kOutcomeUnknown);
    } catch (const DbError&) {
        return Error::storageFailure(kStorageFailed);
    } catch (const InjectedFault&) {
        return Error::storageFailure(kStorageFailed);
    }
}

// ------------------------------------------------------------ free slots

namespace {
std::optional<Error> checkBookableDate(const Date& date, const Date& today, int maxDaysAhead) {
    const std::int64_t d = daysFromCivil(date.year, date.month, date.day);
    const std::int64_t t = daysFromCivil(today.year, today.month, today.day);
    if (d < t) return Error::validation("Date", "is in the past");
    if (d - t > maxDaysAhead) {
        return Error::validation("Date", "bookings are open up to " + std::to_string(maxDaysAhead) + " days ahead");
    }
    return std::nullopt;
}
}  // namespace

std::vector<LocalSlot> AppointmentService::computeFreeSlots(const DoctorSummary& doctor, const Date& date) {
    std::vector<LocalSlot> out;
    if (!doctor.hasProfile) return out;
    const auto hours = doctor.hoursOn(isoWeekday(date));
    if (!hours) return out;
    const auto booked = repo_.bookedForDoctorOn(doctor.staffId, date);
    const std::int64_t nowMin = now().epochMinutes();
    const int len = policy_.appointment.slotMinutes;
    for (int m : gridMinutes(*hours)) {
        const LocalSlot slot{date, m};
        const std::int64_t start = slot.epochMinutes();
        if (start <= nowMin) continue;  // already started
        const bool taken = std::any_of(booked.begin(), booked.end(), [&](const AppointmentRecord& a) {
            return overlaps(start, len, a.start.epochMinutes(), a.slotMinutes);
        });
        if (!taken) out.push_back(slot);
    }
    return out;
}

Result<std::vector<DoctorDay>> AppointmentService::doctorsOn(const Session& session, const Date& date) {
    if (!authorize(session, Permission::ViewAppointments)) {
        return Error::accessDenied("Your role (" + to_string(session.role) + ") may not book appointments.");
    }
    if (auto e = checkBookableDate(date, today(), policy_.appointment.maxDaysAhead)) return *e;
    try {
        std::vector<DoctorDay> out;
        const int weekday = isoWeekday(date);
        for (auto& d : repo_.listDoctors()) {
            if (!d.hasProfile) continue;
            const auto h = d.hoursOn(weekday);
            if (!h) continue;
            DoctorDay day{d, *h, computeFreeSlots(d, date)};
            out.push_back(std::move(day));
        }
        return out;
    } catch (const DbError&) {
        return Error::storageFailure(kReadFailed);
    }
}

Result<std::vector<LocalSlot>> AppointmentService::freeSlots(const Session& session, std::int64_t doctorStaffId,
                                                            const Date& date) {
    if (!authorize(session, Permission::ViewAppointments)) {
        return Error::accessDenied("Your role (" + to_string(session.role) + ") may not book appointments.");
    }
    if (auto e = checkBookableDate(date, today(), policy_.appointment.maxDaysAhead)) return *e;
    try {
        auto doctor = repo_.findDoctor(doctorStaffId);
        if (!doctor) return Error::notFound("No Doctor account with staff number " + std::to_string(doctorStaffId) + ".");
        return computeFreeSlots(*doctor, date);
    } catch (const DbError&) {
        return Error::storageFailure(kReadFailed);
    }
}

// ------------------------------------------------------------ booking rules

std::optional<Error> AppointmentService::checkSlot(const DoctorSummary& doctor, const LocalSlot& start) const {
    if (auto e = checkBookableDate(start.date, today(), policy_.appointment.maxDaysAhead)) return e;
    if (!doctor.hasProfile) {
        return Error::validation("Doctor", doctorLabel(doctor.username) +
                                               " has no working hours yet; an Administrator must set them first");
    }
    const auto hours = doctor.hoursOn(isoWeekday(start.date));
    if (!hours) {
        return Error::validation("Slot", doctorLabel(doctor.username) + " does not work on " +
                                             weekdayName(isoWeekday(start.date)) + "s");
    }
    const auto grid = gridMinutes(*hours);
    if (std::find(grid.begin(), grid.end(), start.minute) == grid.end()) {
        return Error::validation("Slot", start.timeText() + " is not a slot start in " + doctorLabel(doctor.username) +
                                             "'s hours (" + hoursText(*hours) + ", every " +
                                             std::to_string(policy_.appointment.slotMinutes) + " minutes)");
    }
    if (start.epochMinutes() <= now().epochMinutes()) {
        return Error::validation("Slot", start.toDisplay() + " has already started");
    }
    return std::nullopt;
}

std::optional<Error> AppointmentService::checkOverlaps(std::int64_t doctorStaffId, std::int64_t uhid,
                                                       const LocalSlot& start, std::optional<std::int64_t> ignoreId) {
    const std::int64_t s = start.epochMinutes();
    const int len = policy_.appointment.slotMinutes;
    for (const auto& a : repo_.bookedForDoctorOn(doctorStaffId, start.date)) {
        if (ignoreId && a.id == *ignoreId) continue;
        if (overlaps(s, len, a.start.epochMinutes(), a.slotMinutes)) {
            const LocalSlot end = fromEpochMinutes(a.start.epochMinutes() + a.slotMinutes);
            return Error::conflict(doctorLabel(a.doctorUsername) + " already has an appointment from " +
                                   a.start.timeText() + " to " + end.timeText() + " on " + a.start.date.toDisplay() +
                                   ". Nothing was saved and the existing appointment was not changed. "
                                   "Choose another slot.");
        }
    }
    for (const auto& a : repo_.bookedForPatientOn(uhid, start.date)) {
        if (ignoreId && a.id == *ignoreId) continue;
        if (overlaps(s, len, a.start.epochMinutes(), a.slotMinutes)) {
            return Error::conflict("This patient already has an appointment at " + a.start.toDisplay() + " with " +
                                   doctorLabel(a.doctorUsername) + " (" + formatAppointmentId(a.id) +
                                   "). Nothing was saved.");
        }
    }
    return std::nullopt;
}

std::optional<Error> AppointmentService::checkChangeAllowed(const AppointmentRecord& a, const char* verb) const {
    if (a.status != AppointmentStatus::Booked) {
        return Error::conflict(formatAppointmentId(a.id) + " is " + text::asciiLower(to_string(a.status)) +
                               " and cannot be " + verb + ".");
    }
    const std::int64_t minutesLeft = a.start.epochMinutes() - now().epochMinutes();
    if (minutesLeft < policy_.appointment.cutoffMinutes) {
        return Error::conflict(formatAppointmentId(a.id) + " starts at " + a.start.toDisplay() +
                               ". It can no longer be " + verb + ": changes close " +
                               std::to_string(policy_.appointment.cutoffMinutes) + " minutes before the start.");
    }
    return std::nullopt;
}

namespace {
// SQLite reports a unique-index violation with this extended code. The
// appointment unique indexes are the second layer of HMS-F-011.
bool isUniqueViolation(const DbError& e) { return e.code() == SQLITE_CONSTRAINT_UNIQUE; }
const char* kSlotJustTaken =
    "That slot was booked by someone else a moment ago. Nothing was saved and the existing appointment was not "
    "changed. Choose another slot.";
}  // namespace

Result<AppointmentRecord> AppointmentService::book(const Session& session, std::int64_t uhid,
                                                   std::int64_t doctorStaffId, const LocalSlot& start) {
    if (!authorize(session, Permission::BookAppointment)) {
        return Error::accessDenied("Your role (" + to_string(session.role) + ") may not book appointments.");
    }
    try {
        Transaction tx(db_);  // the availability check and the write happen together (HMS-F-011)
        if (!repo_.patientExists(uhid)) return Error::notFound("No patient found with UHID " + formatUhid(uhid) + ".");
        auto doctor = repo_.findDoctor(doctorStaffId);
        if (!doctor) return Error::notFound("No Doctor account with staff number " + std::to_string(doctorStaffId) + ".");
        if (auto e = checkSlot(*doctor, start)) return *e;
        if (auto e = checkOverlaps(doctorStaffId, uhid, start, std::nullopt)) return *e;

        const std::int64_t id = repo_.insertAppointment(uhid, doctorStaffId, start, policy_.appointment.slotMinutes,
                                                        toIsoUtc(clock_.now()), session.userId);
        AuditEntryInput e;
        e.action = audit_action::kAppointmentBook;
        e.recordId = formatAppointmentId(id);
        audit_.append(session.userId, e);
        db_.faultPoint("appointment_before_commit");
        tx.commit();
        return *repo_.findAppointment(id);
    } catch (const CommitUncertain&) {
        return Error::outcomeUnknown(kOutcomeUnknown);
    } catch (const DbError& e) {
        if (isUniqueViolation(e)) return Error::conflict(kSlotJustTaken);
        return Error::storageFailure(kStorageFailed);
    } catch (const InjectedFault&) {
        return Error::storageFailure(kStorageFailed);
    }
}

// ------------------------------------------------------------ changes

Result<std::vector<AppointmentRecord>> AppointmentService::upcomingForPatient(const Session& session,
                                                                             std::int64_t uhid) {
    if (!authorize(session, Permission::ViewAppointments)) {
        return Error::accessDenied("Your role (" + to_string(session.role) + ") may not view appointments.");
    }
    try {
        if (!repo_.patientExists(uhid)) return Error::notFound("No patient found with UHID " + formatUhid(uhid) + ".");
        return repo_.bookedForPatientFrom(uhid, now().toKey());
    } catch (const DbError&) {
        return Error::storageFailure(kReadFailed);
    }
}

Result<AppointmentRecord> AppointmentService::cancel(const Session& session, std::int64_t appointmentId) {
    if (!authorize(session, Permission::CancelAppointment)) {
        return Error::accessDenied("Your role (" + to_string(session.role) + ") may not cancel appointments.");
    }
    try {
        Transaction tx(db_);
        auto a = repo_.findAppointment(appointmentId);
        if (!a) return Error::notFound("No appointment " + formatAppointmentId(appointmentId) + ".");
        if (auto e = checkChangeAllowed(*a, "cancelled")) return *e;

        repo_.markCancelled(a->id, toIsoUtc(clock_.now()), session.userId);
        AuditEntryInput e;
        e.action = audit_action::kAppointmentCancel;
        e.recordId = formatAppointmentId(a->id);
        e.changedFields = {"status"};
        audit_.append(session.userId, e);
        db_.faultPoint("cancel_before_commit");
        tx.commit();
        return *repo_.findAppointment(a->id);  // the slot is free again at once (AC-HMS-F-012)
    } catch (const CommitUncertain&) {
        return Error::outcomeUnknown(kOutcomeUnknown);
    } catch (const DbError&) {
        return Error::storageFailure(kStorageFailed);
    } catch (const InjectedFault&) {
        return Error::storageFailure(kStorageFailed);
    }
}

Result<AppointmentRecord> AppointmentService::reschedule(const Session& session, std::int64_t appointmentId,
                                                         const LocalSlot& newStart) {
    if (!authorize(session, Permission::RescheduleAppointment)) {
        return Error::accessDenied("Your role (" + to_string(session.role) + ") may not reschedule appointments.");
    }
    try {
        Transaction tx(db_);
        auto old = repo_.findAppointment(appointmentId);
        if (!old) return Error::notFound("No appointment " + formatAppointmentId(appointmentId) + ".");
        if (auto e = checkChangeAllowed(*old, "rescheduled")) return *e;
        if (newStart == old->start) return Error::validation("New slot", "is the same as the current one");
        auto doctor = repo_.findDoctor(old->doctorStaffId);
        if (!doctor) return Error::notFound("The appointment's doctor account no longer exists.");
        if (auto e = checkSlot(*doctor, newStart)) return *e;
        if (auto e = checkOverlaps(old->doctorStaffId, old->uhid, newStart, old->id)) return *e;

        const std::string at = toIsoUtc(clock_.now());
        repo_.markCancelled(old->id, at, session.userId);
        const std::int64_t id = repo_.insertAppointment(old->uhid, old->doctorStaffId, newStart,
                                                        policy_.appointment.slotMinutes, at, session.userId);
        repo_.setReplacedBy(old->id, id);

        AuditEntryInput cancelEntry;
        cancelEntry.action = audit_action::kAppointmentCancel;
        cancelEntry.recordId = formatAppointmentId(old->id);
        cancelEntry.changedFields = {"status", "replaced_by"};
        cancelEntry.reasonCode = "RESCHEDULED";
        const std::string cancelEvent = audit_.append(session.userId, cancelEntry);

        AuditEntryInput bookEntry;
        bookEntry.action = audit_action::kAppointmentReschedule;
        bookEntry.recordId = formatAppointmentId(id);
        bookEntry.relatedEventId = cancelEvent;
        audit_.append(session.userId, bookEntry);

        db_.faultPoint("reschedule_before_commit");
        tx.commit();
        return *repo_.findAppointment(id);
    } catch (const CommitUncertain&) {
        return Error::outcomeUnknown(kOutcomeUnknown);
    } catch (const DbError& e) {
        if (isUniqueViolation(e)) return Error::conflict(kSlotJustTaken);
        return Error::storageFailure(kStorageFailed);
    } catch (const InjectedFault&) {
        return Error::storageFailure(kStorageFailed);
    }
}

// ------------------------------------------------------------ reminders

Result<std::vector<ReminderItem>> AppointmentService::remindersDue(const Session& session) {
    if (!authorize(session, Permission::GenerateReminders)) {
        return Error::accessDenied("Your role (" + to_string(session.role) + ") may not issue reminders.");
    }
    try {
        Transaction tx(db_);
        const LocalSlot from = now();
        const LocalSlot to =
            fromEpochMinutes(from.epochMinutes() + static_cast<std::int64_t>(policy_.appointment.reminderLeadHours) * 60);
        const std::string at = toIsoUtc(clock_.now());
        std::vector<ReminderItem> out;
        for (auto& [record, reminded] : repo_.bookedBetween(from.toKey(), to.toKey())) {
            ReminderItem item{record, false};
            if (!reminded) {
                repo_.insertReminder(record.id, at, session.userId);
                AuditEntryInput e;
                e.action = audit_action::kAppointmentReminder;
                e.recordId = formatAppointmentId(record.id);
                audit_.append(session.userId, e);
                item.newlyGenerated = true;
            }
            out.push_back(std::move(item));
        }
        db_.faultPoint("reminders_before_commit");
        tx.commit();
        return out;
    } catch (const CommitUncertain&) {
        return Error::outcomeUnknown(kOutcomeUnknown);
    } catch (const DbError&) {
        return Error::storageFailure(kStorageFailed);
    } catch (const InjectedFault&) {
        return Error::storageFailure(kStorageFailed);
    }
}

// ------------------------------------------------------------ doctor's queue

Result<std::vector<AppointmentRecord>> AppointmentService::myWeekQueue(const Session& session, const Date& from) {
    if (!authorize(session, Permission::ViewOwnQueue)) {
        return Error::accessDenied("Your role (" + to_string(session.role) + ") has no appointment queue.");
    }
    try {
        auto staffId = repo_.doctorIdForUsername(session.userId);
        std::vector<AppointmentRecord> week;
        if (!staffId) return week;
        const std::int64_t first = daysFromCivil(from.year, from.month, from.day);
        for (int i = 0; i < 7; ++i) {
            for (auto& a : repo_.queueFor(*staffId, civilFromDays(first + i))) week.push_back(std::move(a));
        }
        return week;
    } catch (const DbError&) {
        return Error::storageFailure(kReadFailed);
    }
}

Result<std::vector<AppointmentRecord>> AppointmentService::myQueue(const Session& session, const Date& date) {
    if (!authorize(session, Permission::ViewOwnQueue)) {
        return Error::accessDenied("Your role (" + to_string(session.role) + ") has no appointment queue.");
    }
    try {
        // The doctor is the logged-in account; no other doctor's patients can be asked for.
        auto staffId = repo_.doctorIdForUsername(session.userId);
        if (!staffId) return std::vector<AppointmentRecord>{};
        return repo_.queueFor(*staffId, date);
    } catch (const DbError&) {
        return Error::storageFailure(kReadFailed);
    }
}

}  // namespace hms
