#include "record/medical_record_service.hpp"

#include <string>
#include <vector>

#include "core/text.hpp"
#include "patient/uhid.hpp"
#include "validation/input.hpp"

namespace hms {

namespace {
const char* kStorageFailed = "The data store reported an error. Nothing was saved. Please try again.";
const char* kReadFailed = "The data store reported an error. Please try again.";
const char* kOutcomeUnknown =
    "The data store could not confirm whether the medical history was saved. Do not enter it again. "
    "Restart the application and open the patient's medical history.";
const char* kNoCareRelationship = "NO_CARE_RELATIONSHIP";

// Trims and checks the length in characters (not bytes). An empty answer is allowed.
void takeText(std::vector<FieldError>& errors, const std::string& raw, const std::string& field, std::size_t maxChars,
              std::string& out) {
    auto line = input::cleanLine(raw, field);
    if (!line) {
        errors.push_back(line.error().fields.empty() ? FieldError{field, line.error().message}
                                                     : line.error().fields.front());
        return;
    }
    if (text::utf8Length(line.value()) > maxChars) {
        errors.push_back(FieldError{field, "must be at most " + std::to_string(maxChars) + " characters (it has " +
                                               std::to_string(text::utf8Length(line.value())) + ")"});
        return;
    }
    out = line.value();
}
}  // namespace

MedicalRecordService::MedicalRecordService(Database& db, const Clock& clock, AppPolicy policy)
    : db_(db), clock_(clock), policy_(policy), repo_(db), appointments_(db), audit_(db, clock) {}

void MedicalRecordService::auditDenied(const Session& session, const char* action, std::int64_t uhid,
                                       const char* reasonCode) {
    try {
        Transaction tx(db_);
        AuditEntryInput e;
        e.action = action;
        e.recordId = formatUhid(uhid);
        e.outcome = AuditOutcome::Denied;
        e.reasonCode = reasonCode;
        audit_.append(session.userId, e);
        tx.commit();
    } catch (const std::exception&) {
        // The denial stands even if it could not be recorded.
    }
}

bool MedicalRecordService::authorize(const Session& session, Permission permission) {
    if (isAllowed(session.role, permission)) return true;
    try {
        Transaction tx(db_);
        AuditEntryInput e;
        e.action = auditActionFor(permission);
        e.outcome = AuditOutcome::Denied;
        audit_.append(session.userId, e);
        tx.commit();
    } catch (const std::exception&) {
    }
    return false;
}

Result<std::int64_t> MedicalRecordService::careDoctor(const Session& session, std::int64_t uhid, const char* action) {
    auto doctorId = appointments_.doctorIdForUsername(session.userId);
    if (!doctorId || !appointments_.hasCareRelationship(*doctorId, uhid)) {
        auditDenied(session, action, uhid, kNoCareRelationship);
        return Error::accessDenied("You have no booked or completed appointment with patient " + formatUhid(uhid) +
                                   ", so you may not open their medical history.");
    }
    return *doctorId;
}

Result<MedicalHistoryDraft> MedicalRecordService::validate(const MedicalHistoryDraft& draft) const {
    const std::size_t maxChars = policy_.medicalRecord.maxFieldChars;
    std::vector<FieldError> errors;
    MedicalHistoryDraft out;
    takeText(errors, draft.history, "Medical history", maxChars, out.history);
    takeText(errors, draft.allergies, "Allergies", maxChars, out.allergies);
    takeText(errors, draft.currentMedications, "Current medications", maxChars, out.currentMedications);
    if (!errors.empty()) return Error::validation(std::move(errors));
    return out;
}

Result<MedicalHistory> MedicalRecordService::openHistory(const Session& session, std::int64_t uhid) {
    if (!authorize(session, Permission::ViewMedicalHistory)) {
        return Error::accessDenied("Your role (" + to_string(session.role) + ") may not open medical histories.");
    }
    try {
        auto name = repo_.patientName(uhid);
        if (!name) return Error::notFound("No patient found with UHID " + formatUhid(uhid) + ".");
        auto doctorId = careDoctor(session, uhid, audit_action::kMedicalHistoryView);
        if (!doctorId) return doctorId.error();

        Transaction tx(db_);
        MedicalHistory h;
        h.uhid = uhid;
        h.patientName = *name;
        h.versions = repo_.listForPatient(uhid);
        AuditEntryInput e;
        e.action = audit_action::kMedicalHistoryView;
        e.recordId = formatUhid(uhid);
        audit_.append(session.userId, e);
        tx.commit();
        return h;
    } catch (const CommitUncertain&) {
        return Error::storageFailure(kReadFailed);
    } catch (const DbError&) {
        return Error::storageFailure(kReadFailed);
    }
}

Result<MedicalHistoryVersion> MedicalRecordService::updateHistory(const Session& session, std::int64_t uhid,
                                                                  const MedicalHistoryDraft& draft) {
    if (!authorize(session, Permission::UpdateMedicalHistory)) {
        return Error::accessDenied("Your role (" + to_string(session.role) + ") may not update medical histories.");
    }
    auto valid = validate(draft);
    if (!valid) return valid.error();
    const MedicalHistoryDraft& v = valid.value();

    try {
        // Cheap checks first, outside the write transaction.
        if (!repo_.patientName(uhid)) return Error::notFound("No patient found with UHID " + formatUhid(uhid) + ".");
        auto doctorId = careDoctor(session, uhid, audit_action::kMedicalHistoryUpdate);
        if (!doctorId) return doctorId.error();

        Transaction tx(db_);
        // Checked again inside the transaction: the last appointment may have been cancelled meanwhile.
        if (!appointments_.hasCareRelationship(doctorId.value(), uhid)) {
            tx.rollback();
            auditDenied(session, audit_action::kMedicalHistoryUpdate, uhid, kNoCareRelationship);
            return Error::accessDenied("You no longer have a booked or completed appointment with patient " +
                                       formatUhid(uhid) + ". Nothing was saved.");
        }

        // Only the fields that differ from the current version are named in the audit entry.
        const auto versions = repo_.listForPatient(uhid);
        const MedicalHistoryDraft before =
            versions.empty() ? MedicalHistoryDraft{}
                             : MedicalHistoryDraft{versions.front().history, versions.front().allergies,
                                                   versions.front().currentMedications};
        std::vector<std::string> changed;
        if (v.history != before.history) changed.push_back("history");
        if (v.allergies != before.allergies) changed.push_back("allergies");
        if (v.currentMedications != before.currentMedications) changed.push_back("current_medications");
        if (changed.empty()) {
            return Error::validation("Medical history", versions.empty()
                                                            ? "enter the history, allergies or current medications"
                                                            : "nothing was changed, so no new version was saved");
        }

        repo_.insert(uhid, v, doctorId.value(), toIsoUtc(clock_.now()));
        AuditEntryInput e;
        e.action = audit_action::kMedicalHistoryUpdate;
        e.recordId = formatUhid(uhid);
        e.changedFields = changed;
        audit_.append(session.userId, e);
        db_.faultPoint("medical_history_before_commit");
        tx.commit();
        return repo_.listForPatient(uhid).front();  // the version just saved
    } catch (const CommitUncertain&) {
        return Error::outcomeUnknown(kOutcomeUnknown);
    } catch (const DbError&) {
        return Error::storageFailure(kStorageFailed);
    } catch (const InjectedFault&) {
        return Error::storageFailure(kStorageFailed);
    }
}

}  // namespace hms
