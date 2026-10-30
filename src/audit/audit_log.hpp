#pragma once
// Audit Module (SRS HMS-SR-004; SAD 3.4).
//
// Rules implemented:
//  - every entry has a unique random event ID, user ID, UTC timestamp,
//    action, affected record ID and outcome;
//  - an entry for a data change is written in the SAME transaction as the
//    change: append() must be called inside the caller's Transaction, so if
//    the entry cannot be written the change is rolled back with it;
//  - entries hold IDs and field names only - never names, phone numbers,
//    clinical text or free text typed by staff;
//  - no function updates or deletes an entry (the store refuses it too).

#include <optional>
#include <string>
#include <vector>

#include "core/clock.hpp"
#include "data/database.hpp"

namespace hms {

enum class AuditOutcome { Ok, Denied, Failed, Prepared, Published, Abandoned, Started };
std::string to_string(AuditOutcome outcome);

namespace audit_action {
inline constexpr const char* kPatientRegister = "PATIENT_REGISTER";
inline constexpr const char* kPatientUseExisting = "PATIENT_DUPLICATE_USE_EXISTING";
inline constexpr const char* kPatientUpdateDemographics = "PATIENT_UPDATE_DEMOGRAPHICS";
inline constexpr const char* kPatientSearch = "PATIENT_SEARCH";
inline constexpr const char* kPatientView = "PATIENT_VIEW";
// Issue #4. BOOK, CANCEL, RESCHEDULE, REMINDER and SCHEDULE_SET are written for
// every successful change; LIST, VIEW and QUEUE_VIEW only when denied.
inline constexpr const char* kDoctorScheduleSet = "DOCTOR_SCHEDULE_SET";
inline constexpr const char* kDoctorList = "DOCTOR_LIST";
inline constexpr const char* kAppointmentBook = "APPOINTMENT_BOOK";
inline constexpr const char* kAppointmentView = "APPOINTMENT_VIEW";
inline constexpr const char* kAppointmentCancel = "APPOINTMENT_CANCEL";
inline constexpr const char* kAppointmentReschedule = "APPOINTMENT_RESCHEDULE";
inline constexpr const char* kAppointmentReminder = "APPOINTMENT_REMINDER";
inline constexpr const char* kAppointmentQueueView = "APPOINTMENT_QUEUE_VIEW";
// Consultations (issue #5)
inline constexpr const char* kConsultationRecord = "CONSULTATION_RECORD";
inline constexpr const char* kConsultationView = "CONSULTATION_VIEW";
// Billing (issue #7). FEE_SET, BILL_GENERATE, PAYMENT_RECORD and RECEIPT_GENERATE are written for every
// success; BILL_VIEW only when denied.
inline constexpr const char* kConsultationFeeSet = "CONSULTATION_FEE_SET";
inline constexpr const char* kBillGenerate = "BILL_GENERATE";
inline constexpr const char* kBillView = "BILL_VIEW";
inline constexpr const char* kPaymentRecord = "PAYMENT_RECORD";
inline constexpr const char* kReceiptGenerate = "RECEIPT_GENERATE";
// Staff login and accounts (issue #3). Entries hold staff IDs only: never a
// password and never text typed at the login screen.
inline constexpr const char* kStaffLogin = "STAFF_LOGIN";
inline constexpr const char* kStaffLockout = "STAFF_LOCKOUT";
inline constexpr const char* kStaffAccountCreate = "STAFF_ACCOUNT_CREATE";
}  // namespace audit_action

struct AuditEntryInput {
    std::string action;
    std::optional<std::string> recordId;
    AuditOutcome outcome = AuditOutcome::Ok;
    std::vector<std::string> changedFields;  // field names only
    std::optional<std::string> reasonCode;
    std::optional<std::string> relatedEventId;
};

struct AuditEntry {
    std::string eventId;
    std::string userId;
    std::optional<std::string> osUser;
    std::string recordedAt;
    std::string action;
    std::optional<std::string> recordId;
    std::string outcome;
    std::optional<std::string> changedFields;
    std::optional<std::string> reasonCode;
    std::optional<std::string> relatedEventId;
};

class AuditLog {
public:
    AuditLog(Database& db, const Clock& clock) : db_(db), clock_(clock) {}

    // Appends one entry and returns its event ID. Call inside a Transaction.
    std::string append(const std::string& userId, const AuditEntryInput& entry);

    // Newest first (read-only view; the Admin screen comes later).
    std::vector<AuditEntry> recent(std::size_t limit) const;

private:
    Database& db_;
    const Clock& clock_;
};

// Staff login and accounts (issue #3). Entries hold staff IDs only: never a
// password and never text typed at the login screen.
inline constexpr const char* kStaffLogin = "STAFF_LOGIN";                  // OK or DENIED (+ reason code)
inline constexpr const char* kStaffLockout = "STAFF_LOCKOUT";
inline constexpr const char* kStaffAccountCreate = "STAFF_ACCOUNT_CREATE";

}  // namespace hms
