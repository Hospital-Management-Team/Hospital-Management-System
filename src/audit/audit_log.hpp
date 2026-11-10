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
// Laboratory and pharmacy (issue #8). LAB_RESULT_RECORD, MEDICINE_ADD, MEDICINE_UPDATE, STOCK_RESTOCK, STOCK_CORRECT and MEDICINE_DISPENSE are
// written for every success; the view actions only when denied. Entries hold IDs and field names, never
// result text or medicine names.
inline constexpr const char* kLabOrderView = "LAB_ORDER_VIEW";
inline constexpr const char* kLabResultRecord = "LAB_RESULT_RECORD";
inline constexpr const char* kLabResultView = "LAB_RESULT_VIEW";
inline constexpr const char* kMedicineAdd = "MEDICINE_ADD";
inline constexpr const char* kStockView = "STOCK_VIEW";
inline constexpr const char* kStockRestock = "STOCK_RESTOCK";
inline constexpr const char* kMedicineUpdate = "MEDICINE_UPDATE";
inline constexpr const char* kStockCorrect = "STOCK_CORRECT";
inline constexpr const char* kPrescriptionView = "PRESCRIPTION_VIEW";
inline constexpr const char* kMedicineDispense = "MEDICINE_DISPENSE";
// Staff login and accounts (issue #3). Entries hold staff IDs only: never a
// password and never text typed at the login screen.
inline constexpr const char* kStaffLogin = "STAFF_LOGIN";
inline constexpr const char* kStaffLockout = "STAFF_LOCKOUT";
inline constexpr const char* kStaffAccountCreate = "STAFF_ACCOUNT_CREATE";
// Medical history (issue #6). VIEW is written every time a doctor opens a
// patient's history (HMS-SR-004), UPDATE for every new version; both are DENIED
// with reason NO_CARE_RELATIONSHIP for a doctor who has no care relationship.
inline constexpr const char* kMedicalHistoryView = "MEDICAL_HISTORY_VIEW";
inline constexpr const char* kMedicalHistoryUpdate = "MEDICAL_HISTORY_UPDATE";
// Admin's view of the audit log (issue #6). Written only when denied.
inline constexpr const char* kAuditLogView = "AUDIT_LOG_VIEW";
// Admin and reporting (issue #9). The account changes are written for every success; STAFF_ACCOUNT_MANAGE
// and REPORT_VIEW only when denied.
inline constexpr const char* kStaffAccountManage = "STAFF_ACCOUNT_MANAGE";
inline constexpr const char* kStaffAccountDeactivate = "STAFF_ACCOUNT_DEACTIVATE";
inline constexpr const char* kStaffAccountReactivate = "STAFF_ACCOUNT_REACTIVATE";
inline constexpr const char* kStaffAccountUnlock = "STAFF_ACCOUNT_UNLOCK";
inline constexpr const char* kStaffPasswordReset = "STAFF_PASSWORD_RESET";
inline constexpr const char* kReportView = "REPORT_VIEW";
// Master data (issue #9, HMS-F-050). MASTER_DATA_MANAGE only when denied.
inline constexpr const char* kMasterDataManage = "MASTER_DATA_MANAGE";
inline constexpr const char* kDepartmentCreate = "DEPARTMENT_CREATE";
inline constexpr const char* kDepartmentUpdate = "DEPARTMENT_UPDATE";
inline constexpr const char* kRoomCreate = "ROOM_CREATE";
inline constexpr const char* kRoomUpdate = "ROOM_UPDATE";
inline constexpr const char* kDoctorDepartmentSet = "DOCTOR_DEPARTMENT_SET";
// System parameters (HMS-F-052). The record is the parameter name; the old and new value are
// written as the changed field (they are settings, not patient data).
inline constexpr const char* kSettingsUpdate = "SETTINGS_UPDATE";
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

    // Newest first (read-only view; the Admin reads it through AuditLogService).
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
