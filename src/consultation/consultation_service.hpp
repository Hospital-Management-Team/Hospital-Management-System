#pragma once
// Consultation Module - business logic (SRS HMS-F-022; issue #5). Follows the
// Appointment module's contract: the role is checked first (a denial is
// audited), the input is validated, and each change is made in one transaction
// together with its audit entry.
//
//  - The patient and the doctor come from the appointment, never from what the
//    caller types, so a consultation cannot be attached to the wrong patient.
//  - Only the doctor the appointment belongs to can record it, and only while
//    it is Booked. Recording it makes the appointment Completed, so a second
//    consultation for the same appointment is refused.
//  - Audit entries carry the consultation number and field names only; the
//    diagnosis, medicines and tests are never copied into the audit log.

#include <vector>

#include "appointment/appointment_repository.hpp"
#include "audit/audit_log.hpp"
#include "auth/authorizer.hpp"
#include "auth/session.hpp"
#include "config/policy.hpp"
#include "consultation/consultation.hpp"
#include "consultation/consultation_repository.hpp"
#include "core/clock.hpp"
#include "core/result.hpp"
#include "data/database.hpp"

namespace hms {

class ConsultationService {
public:
    ConsultationService(Database& db, const Clock& clock, AppPolicy policy);

    // HMS-F-022 (Doctor).
    Result<ConsultationRecord> recordConsultation(const Session& session, const ConsultationDraft& draft);

    // A patient's earlier consultations, newest first (SRS use case "View Patient History", Doctor).
    Result<std::vector<ConsultationRecord>> listPatientHistory(const Session& session, std::int64_t uhid);

    Date today() const;
    const AppPolicy& policy() const { return policy_; }

private:
    bool authorize(const Session& session, Permission permission);
    void auditDenied(const Session& session, const char* action, const std::string& recordId);
    Result<ValidConsultation> validate(const ConsultationDraft& draft) const;

    Database& db_;
    const Clock& clock_;
    AppPolicy policy_;
    ConsultationRepository repo_;
    AppointmentRepository appointments_;
    AuditLog audit_;
};

}  // namespace hms
