#include "lab/lab_service.hpp"

#include "core/text.hpp"
#include "patient/uhid.hpp"
#include "validation/input.hpp"

namespace hms {

namespace {
constexpr const char* kResult = "Result";
const char* kStorageFailed = "The data store reported an error. Nothing was saved. Please try again.";
const char* kReadFailed = "The data store reported an error. Please try again.";
const char* kOutcomeUnknown =
    "The data store could not confirm whether the change was saved. Do not enter it again. "
    "Restart the application and check the result.";

// Runs a database action and turns storage failures into the error contract.
template <class T, class F>
Result<T> guarded(bool writes, F&& action) {
    try {
        return action();
    } catch (const CommitUncertain&) {
        return writes ? Error::outcomeUnknown(kOutcomeUnknown) : Error::storageFailure(kReadFailed);
    } catch (const DbError&) {
        return Error::storageFailure(writes ? kStorageFailed : kReadFailed);
    } catch (const InjectedFault&) {
        return Error::storageFailure(writes ? kStorageFailed : kReadFailed);
    }
}

Error orderNotFound(std::int64_t id) {
    return Error::notFound("No lab order found with number " + formatLabOrderId(id) + ".");
}
Error patientNotFound(std::int64_t uhid) { return Error::notFound("No patient found with UHID " + formatUhid(uhid) + "."); }
}  // namespace

LabService::LabService(Database& db, const Clock& clock, AppPolicy policy)
    : db_(db), clock_(clock), policy_(policy), repo_(db), appointments_(db), audit_(db, clock) {}

bool LabService::authorize(const Session& session, Permission permission) {
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

Error LabService::denied(const Session& session, const char* what) const {
    return Error::accessDenied("Your role (" + to_string(session.role) + ") may not " + what + ".");
}

Result<std::vector<LabOrderView>> LabService::listPendingOrders(const Session& session) {
    if (!authorize(session, Permission::ViewLabOrders)) return denied(session, "view lab orders");
    return guarded<std::vector<LabOrderView>>(false, [&]() -> Result<std::vector<LabOrderView>> {
        return repo_.listPending(policy_.labPharmacy.maxListRows);
    });
}

Result<std::vector<LabOrderView>> LabService::listOrdersForPatient(const Session& session, std::int64_t uhid) {
    if (!authorize(session, Permission::ViewLabOrders)) return denied(session, "view lab orders");
    return guarded<std::vector<LabOrderView>>(false, [&]() -> Result<std::vector<LabOrderView>> {
        if (!repo_.patientExists(uhid)) return patientNotFound(uhid);
        return repo_.listForPatient(uhid);
    });
}

Result<LabOrderView> LabService::getOrder(const Session& session, std::int64_t orderId) {
    if (!authorize(session, Permission::ViewLabOrders)) return denied(session, "view lab orders");
    return guarded<LabOrderView>(false, [&]() -> Result<LabOrderView> {
        auto o = repo_.findOrder(orderId);
        if (!o) return orderNotFound(orderId);
        return *o;
    });
}

Result<std::vector<LabResultRevision>> LabService::getResultHistory(const Session& session, std::int64_t orderId) {
    if (!authorize(session, Permission::ViewLabOrders)) return denied(session, "view lab orders");
    return guarded<std::vector<LabResultRevision>>(false, [&]() -> Result<std::vector<LabResultRevision>> {
        if (!repo_.findOrder(orderId)) return orderNotFound(orderId);
        return repo_.history(orderId);
    });
}

Result<LabOrderView> LabService::recordResult(const Session& session, std::int64_t orderId,
                                              const std::string& resultText) {
    if (!authorize(session, Permission::RecordLabResult)) return denied(session, "record lab results");
    auto line = input::requireLine(resultText, kResult);
    if (!line) return line.error();
    const std::string value = text::collapseSpaces(line.value());
    const auto len = text::utf8Length(value);
    if (len > policy_.labPharmacy.maxResultChars) {
        return Error::validation(kResult, "must be at most " + std::to_string(policy_.labPharmacy.maxResultChars) +
                                              " characters (it has " + std::to_string(len) + ")");
    }

    return guarded<LabOrderView>(true, [&]() -> Result<LabOrderView> {
        Transaction tx(db_);
        if (!repo_.findOrder(orderId)) return orderNotFound(orderId);
        const auto latest = repo_.latest(orderId);
        if (latest && latest->text == value) {
            return Error::conflict("The result is the same as the one already saved; nothing to update.");
        }
        const int revision = latest ? latest->revision + 1 : 1;
        repo_.insertRevision(orderId, revision, value, session.userId, toIsoUtc(clock_.now()));
        AuditEntryInput e;
        e.action = audit_action::kLabResultRecord;
        e.recordId = formatLabOrderId(orderId);
        e.changedFields = {"result"};
        audit_.append(session.userId, e);
        db_.faultPoint("lab_result_before_commit");
        tx.commit();
        return *repo_.findOrder(orderId);
    });
}

Result<std::vector<LabOrderView>> LabService::listResultsForPatient(const Session& session, std::int64_t uhid) {
    if (!authorize(session, Permission::ViewLabResults)) return denied(session, "view lab results");
    return guarded<std::vector<LabOrderView>>(false, [&]() -> Result<std::vector<LabOrderView>> {
        const auto doctorId = appointments_.doctorIdForUsername(session.userId);
        if (!doctorId) return Error::accessDenied("Your account is not a doctor account.");
        if (!repo_.patientExists(uhid)) return patientNotFound(uhid);
        return repo_.listForPatientByDoctor(uhid, *doctorId);
    });
}

}  // namespace hms
