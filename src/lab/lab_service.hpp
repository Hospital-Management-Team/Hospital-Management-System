#pragma once
// Laboratory Module business logic (SRS HMS-F-040; SAD 3.4; issue #8). Follows
// the Appointment, Consultation and Billing contract: the role is checked
// first (a denial is audited), the input is validated, and each change is made
// in one transaction together with its audit entry.
//
//  - Lab Technician: lists the tests still waiting for a result, finds a
//    patient's tests, enters a result and updates it later. Every update is
//    kept as a new revision, so the earlier text is never lost.
//  - Doctor: reads the results of the tests the doctor ordered, for one
//    patient (AC-HMS-F-040: visible to the treating doctor).
//  - The result is linked to the test order the doctor wrote on the
//    consultation (D-43). Audit entries hold IDs and field names only, never
//    the result text.

#include <string>
#include <vector>

#include "appointment/appointment_repository.hpp"
#include "audit/audit_log.hpp"
#include "auth/authorizer.hpp"
#include "auth/session.hpp"
#include "config/policy.hpp"
#include "core/clock.hpp"
#include "core/result.hpp"
#include "data/database.hpp"
#include "lab/lab.hpp"
#include "lab/lab_repository.hpp"

namespace hms {

class LabService {
public:
    LabService(Database& db, const Clock& clock, AppPolicy policy);

    // Lab Technician.
    Result<std::vector<LabOrderView>> listPendingOrders(const Session& session);
    Result<std::vector<LabOrderView>> listOrdersForPatient(const Session& session, std::int64_t uhid);
    Result<LabOrderView> getOrder(const Session& session, std::int64_t orderId);
    Result<std::vector<LabResultRevision>> getResultHistory(const Session& session, std::int64_t orderId);
    // The first result, or an update of the saved one. The text must differ from the latest revision.
    Result<LabOrderView> recordResult(const Session& session, std::int64_t orderId, const std::string& resultText);

    // Doctor: the tests this doctor ordered for one patient, with their results.
    Result<std::vector<LabOrderView>> listResultsForPatient(const Session& session, std::int64_t uhid);

    const AppPolicy& policy() const { return policy_; }

private:
    bool authorize(const Session& session, Permission permission);
    Error denied(const Session& session, const char* what) const;

    Database& db_;
    const Clock& clock_;
    AppPolicy policy_;
    LabRepository repo_;
    AppointmentRepository appointments_;
    AuditLog audit_;
};

}  // namespace hms
