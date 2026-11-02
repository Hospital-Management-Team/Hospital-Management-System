#pragma once
// Medical Records Module - business logic (SRS HMS-F-003, HMS-SR-001, HMS-SR-004;
// issue #6). Follows the contract of the other modules: the role is checked
// first (a denial is audited), the input is validated, and each change is made
// in one transaction together with its audit entry.
//
//  - Only a Doctor who has a care relationship with the patient (SRS 1.4: a
//    Booked or Completed appointment with them) can open or update the history.
//    Having written a version gives no access of its own. Any other doctor is
//    denied, and the denial is audited.
//  - Opening a history is audited, even when the patient has no version yet.
//  - An update saves a new version; earlier versions are kept unchanged.
//  - Audit entries carry the UHID and field names only; the clinical text is
//    never copied into the audit log.

#include "appointment/appointment_repository.hpp"
#include "audit/audit_log.hpp"
#include "auth/authorizer.hpp"
#include "auth/session.hpp"
#include "config/policy.hpp"
#include "core/clock.hpp"
#include "core/result.hpp"
#include "data/database.hpp"
#include "record/medical_record.hpp"
#include "record/medical_record_repository.hpp"

namespace hms {

class MedicalRecordService {
public:
    MedicalRecordService(Database& db, const Clock& clock, AppPolicy policy);

    // HMS-F-003 (Doctor with a care relationship): every version, newest first.
    Result<MedicalHistory> openHistory(const Session& session, std::int64_t uhid);

    // HMS-F-003 (Doctor with a care relationship): saves the draft as a new version.
    Result<MedicalHistoryVersion> updateHistory(const Session& session, std::int64_t uhid,
                                                const MedicalHistoryDraft& draft);

    const AppPolicy& policy() const { return policy_; }

private:
    bool authorize(const Session& session, Permission permission);
    void auditDenied(const Session& session, const char* action, std::int64_t uhid, const char* reasonCode);
    // The doctor's staff ID if the doctor has a care relationship with the patient;
    // otherwise the denial is audited and an error returned.
    Result<std::int64_t> careDoctor(const Session& session, std::int64_t uhid, const char* action);
    Result<MedicalHistoryDraft> validate(const MedicalHistoryDraft& draft) const;

    Database& db_;
    const Clock& clock_;
    AppPolicy policy_;
    MedicalRecordRepository repo_;
    AppointmentRepository appointments_;
    AuditLog audit_;
};

}  // namespace hms
