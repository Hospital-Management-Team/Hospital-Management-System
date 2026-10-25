#pragma once
// Patient Management Module - business logic (SRS HMS-F-001, F-002,
// F-004; SAD 4.2.2 and 4.4.1).
//
// Registration handshake (HMS-F-002(9)):
//   1. registerPatient(draft, DuplicateDecision::none())
//        -> Registered(uhid)                     no candidate: done
//        -> NeedsDecision(candidates)            nothing saved; show review screen
//   2a. staff say "same patient":
//        useExistingPatient(draft, chosenUhid, reviewedUhids) -> existing record, nothing created
//   2b. staff say "different person":
//        registerPatient(draft, {DifferentPerson, reason?, reviewedUhids})
//        -> Registered(uhid), or NeedsDecision again if a candidate outside the
//           reviewed set appeared meanwhile (marked newSinceReview).
// No transaction stays open while staff decide; the candidate search is
// repeated inside the saving transaction.

#include <cstdint>
#include <string_view>
#include <vector>

#include "audit/audit_log.hpp"
#include "auth/authorizer.hpp"
#include "auth/session.hpp"
#include "config/policy.hpp"
#include "core/clock.hpp"
#include "core/result.hpp"
#include "data/database.hpp"
#include "patient/patient.hpp"
#include "patient/patient_repository.hpp"

namespace hms {

class PatientService {
public:
    PatientService(Database& db, const Clock& clock, AppPolicy policy);

    Result<RegistrationOutcome> registerPatient(const Session& session, const PatientDraft& draft,
                                                const DuplicateDecision& decision);

    Result<PatientRecord> useExistingPatient(const Session& session, const PatientDraft& draft,
                                             std::int64_t chosenUhid,
                                             const std::vector<std::int64_t>& reviewedUhids);

    Result<SearchResult> searchPatients(const Session& session, SearchMode mode, std::string_view query);

    Result<PatientRecord> getPatient(const Session& session, std::int64_t uhid);

    // Hospital-local "today" from the injected clock.
    Date today() const;

    const AppPolicy& policy() const { return policy_; }

private:
    // Checks the permission; a denial is audited (own transaction) and false returned.
    bool authorize(const Session& session, Permission permission);
    std::vector<Candidate> findCandidates(const NewPatient& p, std::optional<std::int64_t> excludeUhid);

    Database& db_;
    const Clock& clock_;
    AppPolicy policy_;
    PatientRepository repo_;
    AuditLog audit_;
};

}  // namespace hms
