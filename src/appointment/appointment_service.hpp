#pragma once
// Appointment Module - business logic (SRS HMS-F-010..F-013, F-020 minimal,
// F-021; issue #4). Follows the PatientService contract: every function takes
// the Session, checks the role first (a denial is audited), validates input,
// and makes each change in one transaction together with its audit entry.
//
// Double-booking prevention (HMS-F-011) has two layers:
//  1. inside the booking transaction the service refuses any slot that
//     overlaps a Booked appointment of the same doctor or the same patient;
//  2. the store's partial unique indexes refuse a second Booked appointment
//     at the same start even if the first layer were bypassed.
// The existing appointment is never changed by a refused booking.

#include <cstdint>
#include <optional>
#include <vector>

#include "appointment/appointment.hpp"
#include "appointment/appointment_repository.hpp"
#include "audit/audit_log.hpp"
#include "auth/authorizer.hpp"
#include "auth/session.hpp"
#include "config/policy.hpp"
#include "core/clock.hpp"
#include "core/result.hpp"
#include "data/database.hpp"

namespace hms {

class AppointmentService {
public:
    AppointmentService(Database& db, const Clock& clock, AppPolicy policy);

    // ---- Doctor schedules (HMS-F-020, Admin) ----
    Result<std::vector<DoctorSummary>> listDoctors(const Session& session);
    Result<ScheduleSaved> setDoctorSchedule(const Session& session, std::int64_t doctorStaffId,
                                            const DoctorScheduleDraft& draft);

    // ---- Booking (HMS-F-010, F-011, Receptionist) ----
    // Doctors who work on `date`, each with that day's free slots. A doctor
    // appears only on the weekdays in their declared hours (AC-HMS-F-020).
    Result<std::vector<DoctorDay>> doctorsOn(const Session& session, const Date& date);
    Result<std::vector<LocalSlot>> freeSlots(const Session& session, std::int64_t doctorStaffId, const Date& date);
    Result<AppointmentRecord> book(const Session& session, std::int64_t uhid, std::int64_t doctorStaffId,
                                   const LocalSlot& start);

    // ---- Changes (HMS-F-012, Receptionist) ----
    // Booked appointments of a patient that have not started yet.
    Result<std::vector<AppointmentRecord>> upcomingForPatient(const Session& session, std::int64_t uhid);
    Result<AppointmentRecord> cancel(const Session& session, std::int64_t appointmentId);
    // Same doctor and patient, new start. One transaction: the old appointment
    // is cancelled with `replacedBy` set, and the new one is booked.
    Result<AppointmentRecord> reschedule(const Session& session, std::int64_t appointmentId, const LocalSlot& newStart);

    // ---- Reminders (HMS-F-013, Receptionist) ----
    // Booked appointments starting within the reminder window. A reminder entry
    // is created (and audited) for each one that has none yet.
    Result<std::vector<ReminderItem>> remindersDue(const Session& session);

    // ---- Doctor's queue (HMS-F-021, Doctor) ----
    // The logged-in doctor's appointments on `date`, in time order; other
    // doctors' patients are never included.
    Result<std::vector<AppointmentRecord>> myQueue(const Session& session, const Date& date);

    Date today() const;
    LocalSlot now() const;
    const AppPolicy& policy() const { return policy_; }

private:
    bool authorize(const Session& session, Permission permission);
    // Every slot start for one doctor's working window, on the slot grid.
    std::vector<int> gridMinutes(const WeeklyHours& hours) const;
    std::vector<LocalSlot> computeFreeSlots(const DoctorSummary& doctor, const Date& date);
    // Checks that `start` is a slot the doctor offers and that it has not passed.
    std::optional<Error> checkSlot(const DoctorSummary& doctor, const LocalSlot& start) const;
    // Booked appointments of `doctor` or `uhid` that overlap [start, start+len),
    // ignoring `ignoreId`. Returns a Conflict error if there is one.
    std::optional<Error> checkOverlaps(std::int64_t doctorStaffId, std::int64_t uhid, const LocalSlot& start,
                                       std::optional<std::int64_t> ignoreId);
    // The cutoff rule for a change to an existing appointment.
    std::optional<Error> checkChangeAllowed(const AppointmentRecord& a, const char* verb) const;
    std::optional<Error> validateSchedule(const DoctorScheduleDraft& draft) const;

    Database& db_;
    const Clock& clock_;
    AppPolicy policy_;
    AppointmentRepository repo_;
    AuditLog audit_;
};

}  // namespace hms
