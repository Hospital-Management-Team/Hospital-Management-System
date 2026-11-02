#pragma once
// Audit Module - the Admin's read-only view of the audit log (SRS HMS-SR-004:
// "Admin shall be able to view them (read only)"; issue #6).
//
//  - Only the Admin may read the log; any other role is denied and the denial is
//    audited, like every other protected operation (HMS-SR-001).
//  - Reading changes nothing. There is no function here, or anywhere else, that
//    edits or deletes an entry (the store refuses it too).

#include <cstddef>
#include <vector>

#include "audit/audit_log.hpp"
#include "auth/session.hpp"
#include "config/policy.hpp"
#include "core/clock.hpp"
#include "core/result.hpp"
#include "data/database.hpp"

namespace hms {

class AuditLogService {
public:
    AuditLogService(Database& db, const Clock& clock, AppPolicy policy);

    // The newest `count` entries, newest first (1 to policy.auditView.maxEntries).
    Result<std::vector<AuditEntry>> recentEntries(const Session& session, std::size_t count);

    const AppPolicy& policy() const { return policy_; }

private:
    Database& db_;
    AppPolicy policy_;
    AuditLog audit_;
};

}  // namespace hms
