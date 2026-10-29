#include "consultation/consultation_service.hpp"

#include <string>

#include "core/text.hpp"
#include "patient/uhid.hpp"
#include "validation/input.hpp"

namespace hms {

namespace {
const char* kStorageFailed = "The data store reported an error. Nothing was saved. Please try again.";
const char* kReadFailed = "The data store reported an error. Please try again.";
const char* kOutcomeUnknown =
    "The data store could not confirm whether the consultation was saved. Do not enter it again. "
    "Restart the application and check the patient's consultations.";

// Trims, requires text, and checks the length in characters (not bytes).
void takeText(std::vector<FieldError>& errors, const std::string& raw, const std::string& field, std::size_t maxChars,
              bool required, std::string& out) {
    auto line = required ? input::requireLine(raw, field) : input::cleanLine(raw, field);
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

ConsultationService::ConsultationService(Database& db, const Clock& clock, AppPolicy policy)
    : db_(db), clock_(clock), policy_(policy), repo_(db), appointments_(db), audit_(db, clock) {}

Date ConsultationService::today() const { return localDate(clock_.now(), policy_.utcOffsetMinutes); }

void ConsultationService::auditDenied(const Session& session, const char* action, const std::string& recordId) {
    try {
        Transaction tx(db_);
        AuditEntryInput e;
        e.action = action;
        e.recordId = recordId;
        e.outcome = AuditOutcome::Denied;
        audit_.append(session.userId, e);
        tx.commit();
    } catch (const std::exception&) {
        // The denial stands even if it could not be recorded.
    }
}

bool ConsultationService::authorize(const Session& session, Permission permission) {
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

Result<ValidConsultation> ConsultationService::validate(const ConsultationDraft& draft) const {
    const ConsultationPolicy& cp = policy_.consultation;
    std::vector<FieldError> errors;
    ValidConsultation out;
    out.appointmentId = draft.appointmentId;
    if (draft.appointmentId <= 0) errors.push_back(FieldError{"Appointment", "must be chosen"});
    takeText(errors, draft.diagnosis, "Diagnosis", cp.maxDiagnosisChars, true, out.diagnosis);

    if (draft.prescriptions.size() > cp.maxItemsPerConsultation) {
        errors.push_back(FieldError{"Prescription", "has too many lines (at most " +
                                                        std::to_string(cp.maxItemsPerConsultation) + ")"});
    }
    if (draft.labOrders.size() > cp.maxItemsPerConsultation) {
        errors.push_back(FieldError{"Test orders", "has too many tests (at most " +
                                                       std::to_string(cp.maxItemsPerConsultation) + ")"});
    }
    {
        for (std::size_t i = 0; i < draft.prescriptions.size() && i < cp.maxItemsPerConsultation; ++i) {
            const auto& p = draft.prescriptions[i];
            const std::string label = "Prescription line " + std::to_string(i + 1);
            ValidConsultation::Rx rx;
            takeText(errors, p.medicineName, label + " medicine", cp.maxNameChars, true, rx.medicineName);
            takeText(errors, p.dosage, label + " dosage", cp.maxDosageChars, true, rx.dosage);
            auto q = input::parseWholeNumber(text::trimSpaces(p.quantity), label + " quantity", 1, cp.maxQuantity);
            if (!q) errors.push_back(q.error().fields.empty() ? FieldError{label + " quantity", q.error().message}
                                                              : q.error().fields.front());
            else rx.quantity = q.value();
            out.prescriptions.push_back(std::move(rx));
        }
        for (std::size_t i = 0; i < draft.labOrders.size() && i < cp.maxItemsPerConsultation; ++i) {
            const auto& l = draft.labOrders[i];
            const std::string label = "Test order " + std::to_string(i + 1);
            ValidConsultation::Lab lab;
            takeText(errors, l.testName, label + " test", cp.maxNameChars, true, lab.testName);
            std::string instructions;
            takeText(errors, l.instructions, label + " instructions", cp.maxInstructionChars, false, instructions);
            if (!instructions.empty()) lab.instructions = instructions;
            out.labOrders.push_back(std::move(lab));
        }
    }
    if (!errors.empty()) return Error::validation(std::move(errors));
    return out;
}

Result<ConsultationRecord> ConsultationService::recordConsultation(const Session& session,
                                                                   const ConsultationDraft& draft) {
    if (!authorize(session, Permission::RecordConsultation)) {
        return Error::accessDenied("Your role (" + to_string(session.role) + ") may not record consultations.");
    }
    auto valid = validate(draft);
    if (!valid) return valid.error();
    const std::string apptText = formatAppointmentId(draft.appointmentId);

    try {
        // Cheap checks first, outside the write transaction.
        auto appt = appointments_.findAppointment(draft.appointmentId);
        if (!appt) return Error::notFound("No appointment found with number " + apptText + ".");
        auto doctorId = appointments_.doctorIdForUsername(session.userId);
        if (!doctorId) return Error::notFound("No doctor profile is set up for your account. Ask an Administrator.");
        if (appt->doctorStaffId != *doctorId) {
            auditDenied(session, audit_action::kConsultationRecord, apptText);
            return Error::accessDenied("Appointment " + apptText + " belongs to another doctor.");
        }

        Transaction tx(db_);
        // Checked again inside the transaction, so two sessions cannot both record the same visit.
        appt = appointments_.findAppointment(draft.appointmentId);
        if (!appt) return Error::notFound("No appointment found with number " + apptText + ".");
        if (appt->status == AppointmentStatus::Cancelled) {
            return Error::conflict("Appointment " + apptText + " was cancelled; there is nothing to record.");
        }
        if (appt->status == AppointmentStatus::Completed || repo_.existsForAppointment(appt->id)) {
            return Error::conflict("A consultation has already been recorded for appointment " + apptText + ".");
        }
        if (appt->status != AppointmentStatus::Booked) {
            return Error::conflict("Appointment " + apptText + " is not a booked appointment (" +
                                   to_string(appt->status) + ").");
        }

        valid.value().uhid = appt->uhid;  // the patient comes from the appointment
        valid.value().doctorStaffId = *doctorId;
        const std::int64_t id = repo_.insert(valid.value(), session.userId, toIsoUtc(clock_.now()));
        appointments_.markCompleted(appt->id);

        AuditEntryInput e;
        e.action = audit_action::kConsultationRecord;
        e.recordId = formatConsultationId(id);
        e.changedFields = {"diagnosis", "prescriptions", "lab_orders", "appointment_status"};
        audit_.append(session.userId, e);
        db_.faultPoint("consultation_before_commit");
        tx.commit();
        return *repo_.findById(id);
    } catch (const CommitUncertain&) {
        return Error::outcomeUnknown(kOutcomeUnknown);
    } catch (const DbError&) {
        return Error::storageFailure(kStorageFailed);
    } catch (const InjectedFault&) {
        return Error::storageFailure(kStorageFailed);
    }
}

Result<std::vector<ConsultationRecord>> ConsultationService::listPatientHistory(const Session& session,
                                                                                std::int64_t uhid) {
    if (!authorize(session, Permission::ViewConsultations)) {
        return Error::accessDenied("Your role (" + to_string(session.role) + ") may not view consultations.");
    }
    try {
        Transaction tx(db_);
        if (!repo_.patientExists(uhid)) return Error::notFound("No patient found with UHID " + formatUhid(uhid) + ".");
        auto rows = repo_.listForPatient(uhid);
        AuditEntryInput e;
        e.action = audit_action::kConsultationView;
        e.recordId = formatUhid(uhid);
        audit_.append(session.userId, e);
        tx.commit();
        return rows;
    } catch (const CommitUncertain&) {
        return Error::storageFailure(kReadFailed);
    } catch (const DbError&) {
        return Error::storageFailure(kReadFailed);
    }
}

}  // namespace hms
