#include "audit/audit_log_service.hpp"

#include <string>

#include "auth/authorizer.hpp"

namespace hms {

namespace {
const char* kReadFailed = "The data store reported an error. Please try again.";
}  // namespace

AuditLogService::AuditLogService(Database& db, const Clock& clock, AppPolicy policy)
    : db_(db), policy_(policy), audit_(db, clock) {}

Result<std::vector<AuditEntry>> AuditLogService::recentEntries(const Session& session, std::size_t count) {
    if (!isAllowed(session.role, Permission::ViewAuditLog)) {
        try {
            Transaction tx(db_);
            AuditEntryInput e;
            e.action = auditActionFor(Permission::ViewAuditLog);
            e.outcome = AuditOutcome::Denied;
            audit_.append(session.userId, e);
            tx.commit();
        } catch (const std::exception&) {
            // The denial stands even if it could not be recorded.
        }
        return Error::accessDenied("Your role (" + to_string(session.role) + ") may not view the audit log.");
    }
    const std::size_t maxEntries = policy_.auditView.maxEntries;
    if (count < 1 || count > maxEntries) {
        return Error::validation("Number of entries", "must be from 1 to " + std::to_string(maxEntries));
    }
    try {
        return audit_.recent(count);
    } catch (const DbError&) {
        return Error::storageFailure(kReadFailed);
    }
}

}  // namespace hms
