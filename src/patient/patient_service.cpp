#include "patient/patient_service.hpp"

#include <algorithm>
#include <set>

#include "patient/uhid.hpp"
#include "core/text.hpp"
#include "patient/patient_validation.hpp"
#include "patient/phone.hpp"
#include "validation/input.hpp"

namespace hms {

namespace {
const char* kStorageFailed = "The data store reported an error. Nothing was saved. Please try again.";
const char* kOutcomeUnknown =
    "The data store could not confirm whether the change was saved. Do not enter it again. "
    "Restart the application and search for the record to check.";

bool hasStrong(const std::vector<Candidate>& cs) {
    return std::any_of(cs.begin(), cs.end(), [](const Candidate& c) { return c.kind == CandidateKind::Strong; });
}
bool hasSamePhone(const std::vector<Candidate>& cs) {
    return std::any_of(cs.begin(), cs.end(), [](const Candidate& c) { return c.kind == CandidateKind::SamePhone; });
}
}  // namespace

PatientService::PatientService(Database& db, const Clock& clock, AppPolicy policy)
    : db_(db), clock_(clock), policy_(policy), repo_(db), audit_(db, clock) {}

Date PatientService::today() const { return localDate(clock_.now(), policy_.utcOffsetMinutes); }

bool PatientService::authorize(const Session& session, Permission permission) {
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

std::vector<Candidate> PatientService::findCandidates(const NewPatient& p, std::optional<std::int64_t> excludeUhid) {
    std::vector<Candidate> out;
    std::set<std::int64_t> strongIds;
    for (auto& r : repo_.findStrong(p.normalisedName, p.dateOfBirth, p.gender, excludeUhid)) {
        strongIds.insert(r.uhid);
        out.push_back(Candidate{std::move(r), CandidateKind::Strong, false});
    }
    if (p.phone) {
        // A patient who is a strong candidate is listed once, as strong.
        for (auto& r : repo_.findByPhone(*p.phone, excludeUhid)) {
            if (strongIds.count(r.uhid) == 0) out.push_back(Candidate{std::move(r), CandidateKind::SamePhone, false});
        }
    }
    return out;
}

Result<RegistrationOutcome> PatientService::registerPatient(const Session& session, const PatientDraft& draft,
                                                            const DuplicateDecision& decision) {
    if (!authorize(session, Permission::RegisterPatient)) {
        return Error::accessDenied("Your role (" + to_string(session.role) + ") may not register patients.");
    }
    auto valid = patient_rules::validatePatient(draft, today(), policy_.patient);
    if (!valid) return valid.error();
    const NewPatient& p = valid.value();

    try {
        Transaction tx(db_);
        std::vector<Candidate> candidates = findCandidates(p, std::nullopt);

        if (!candidates.empty()) {
            const std::set<std::int64_t> reviewed(decision.reviewedUhids.begin(), decision.reviewedUhids.end());
            bool anyNew = false;
            if (decision.kind != DuplicateDecision::Kind::None) {
                for (auto& c : candidates) {
                    c.newSinceReview = reviewed.count(c.patient.uhid) == 0;
                    anyNew = anyNew || c.newSinceReview;
                }
            }
            const bool strong = hasStrong(candidates);
            const bool allowed = policy_.patient.phoneMatchAllowsNewPatient || !hasSamePhone(candidates);

            if (decision.kind == DuplicateDecision::Kind::None || anyNew) {
                tx.rollback();  // nothing saved; no transaction open while staff decide
                RegistrationOutcome out;
                out.status = RegistrationOutcome::Status::NeedsDecision;
                out.candidates = std::move(candidates);
                out.reasonRequired = strong;
                out.differentPersonAllowed = allowed;
                return out;
            }
            if (!allowed) {
                return Error::conflict(
                    "Hospital policy: a patient who shares this mobile number with an existing patient must be "
                    "registered under the existing UHID.");
            }
            if (strong && !decision.reason) {
                return Error::validation("Duplicate reason",
                                         "is required because a patient with the same name, date of birth and "
                                         "gender already exists");
            }
        }

        const std::int64_t uhid = repo_.insert(p);
        AuditEntryInput e;
        e.action = audit_action::kPatientRegister;
        e.recordId = formatUhid(uhid);
        e.outcome = AuditOutcome::Ok;
        if (hasStrong(candidates) && decision.reason) e.reasonCode = reasonCode(*decision.reason);
        const std::string eventId = audit_.append(session.userId, e);
        if (!candidates.empty()) {
            repo_.insertDuplicateReview(eventId, "DIFFERENT_PERSON", std::nullopt, uhid, candidates);
        }
        db_.faultPoint("register_before_commit");
        tx.commit();

        RegistrationOutcome out;
        out.status = RegistrationOutcome::Status::Registered;
        out.uhid = uhid;
        return out;
    } catch (const CommitUncertain&) {
        return Error::outcomeUnknown(kOutcomeUnknown);
    } catch (const DbError&) {
        return Error::storageFailure(kStorageFailed);
    } catch (const InjectedFault&) {
        return Error::storageFailure(kStorageFailed);
    }
}

Result<PatientRecord> PatientService::useExistingPatient(const Session& session, const PatientDraft& draft,
                                                         std::int64_t chosenUhid,
                                                         const std::vector<std::int64_t>& reviewedUhids) {
    if (!authorize(session, Permission::RegisterPatient)) {
        return Error::accessDenied("Your role (" + to_string(session.role) + ") may not register patients.");
    }
    auto valid = patient_rules::validatePatient(draft, today(), policy_.patient);
    if (!valid) return valid.error();

    const bool wasShown =
        std::find(reviewedUhids.begin(), reviewedUhids.end(), chosenUhid) != reviewedUhids.end();
    if (!wasShown) {
        return Error::validation("Existing patient", "must be one of the candidates shown on the review screen");
    }

    try {
        Transaction tx(db_);
        std::vector<Candidate> candidates = findCandidates(valid.value(), std::nullopt);
        auto chosen = std::find_if(candidates.begin(), candidates.end(),
                                   [&](const Candidate& c) { return c.patient.uhid == chosenUhid; });
        if (chosen == candidates.end()) {
            return Error::validation("Existing patient", formatUhid(chosenUhid) + " is not a candidate for this patient");
        }
        PatientRecord record = chosen->patient;

        AuditEntryInput e;
        e.action = audit_action::kPatientUseExisting;
        e.recordId = formatUhid(chosenUhid);
        e.outcome = AuditOutcome::Ok;
        const std::string eventId = audit_.append(session.userId, e);
        repo_.insertDuplicateReview(eventId, "USE_EXISTING", chosenUhid, std::nullopt, candidates);
        tx.commit();
        return record;
    } catch (const CommitUncertain&) {
        return Error::outcomeUnknown(kOutcomeUnknown);
    } catch (const DbError&) {
        return Error::storageFailure(kStorageFailed);
    } catch (const InjectedFault&) {
        return Error::storageFailure(kStorageFailed);
    }
}

Result<SearchResult> PatientService::searchPatients(const Session& session, SearchMode mode, std::string_view query) {
    if (!authorize(session, Permission::SearchPatients)) {
        return Error::accessDenied("Your role (" + to_string(session.role) + ") may not search patients.");
    }
    try {
        SearchResult result;
        switch (mode) {
            case SearchMode::Uhid: {
                auto line = input::requireLine(query, "UHID");
                if (!line) return line.error();
                auto id = parseUhid(line.value());
                if (!id) return Error::validation("UHID", "must look like HMS-000123 or 123");
                if (auto r = repo_.findByUhid(*id)) result.rows.push_back(*r);
                result.totalMatches = result.rows.size();
                break;
            }
            case SearchMode::Name: {
                auto line = input::requireLine(query, "Name");
                if (!line) return line.error();
                const std::string norm = text::normaliseName(line.value());
                result.totalMatches = repo_.countByNameContains(norm);
                result.rows = repo_.findByNameContains(norm, policy_.patient.maxSearchResults);
                break;
            }
            case SearchMode::Phone: {
                auto line = input::requireLine(query, phone::kField);
                if (!line) return line.error();
                auto canon = phone::canonicalise(line.value());
                if (!canon) return canon.error();  // never matched as partial digits
                result.rows = repo_.findByPhone(canon.value(), std::nullopt);
                result.totalMatches = result.rows.size();
                break;
            }
        }
        return result;
    } catch (const DbError&) {
        return Error::storageFailure("The data store reported an error while searching. Please try again.");
    }
}

Result<SearchResult> PatientService::searchByNameAndDob(const Session& session, const NameDobQuery& query) {
    if (!authorize(session, Permission::SearchPatients)) {
        return Error::accessDenied("Your role (" + to_string(session.role) + ") may not search patients.");
    }
    std::vector<FieldError> errors;
    auto name = input::requireLine(query.name, "Name");
    if (!name) errors.insert(errors.end(), name.error().fields.begin(), name.error().fields.end());
    auto dob = input::parseDate(query.dateOfBirth, "Date of birth");
    if (!dob) errors.insert(errors.end(), dob.error().fields.begin(), dob.error().fields.end());
    if (!errors.empty()) return Error::validation(std::move(errors));
    try {
        const std::string norm = text::normaliseName(name.value());
        SearchResult result;
        result.totalMatches = repo_.countByNameAndDob(norm, dob.value());
        result.rows = repo_.findByNameAndDob(norm, dob.value(), policy_.patient.maxSearchResults);
        return result;
    } catch (const DbError&) {
        return Error::storageFailure("The data store reported an error while searching. Please try again.");
    }
}

Result<PatientRecord> PatientService::getPatient(const Session& session, std::int64_t uhid) {
    if (!authorize(session, Permission::ViewPatientDemographics)) {
        return Error::accessDenied("Your role (" + to_string(session.role) + ") may not view patient records.");
    }
    try {
        auto r = repo_.findByUhid(uhid);
        if (!r) return Error::notFound("No patient found with UHID " + formatUhid(uhid) + ".");
        return *r;
    } catch (const DbError&) {
        return Error::storageFailure("The data store reported an error. Please try again.");
    }
}

}  // namespace hms
