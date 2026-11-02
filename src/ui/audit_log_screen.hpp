#pragma once
// Admin screen: the audit log, read only (HMS-SR-004, issue #6). Holds no
// business rules: AuditLogService checks the role and reads the entries.

#include "audit/audit_log_service.hpp"
#include "auth/session.hpp"
#include "ui/console_io.hpp"

namespace hms {

void auditLogScreen(ConsoleIO& io, AuditLogService& auditLog, const Session& session);

}  // namespace hms
