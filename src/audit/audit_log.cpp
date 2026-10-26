#include "audit/audit_log.hpp"

#include "core/ids.hpp"

namespace hms {

std::string to_string(AuditOutcome outcome) {
    switch (outcome) {
        case AuditOutcome::Ok: return "OK";
        case AuditOutcome::Denied: return "DENIED";
        case AuditOutcome::Failed: return "FAILED";
        case AuditOutcome::Prepared: return "PREPARED";
        case AuditOutcome::Published: return "PUBLISHED";
        case AuditOutcome::Abandoned: return "ABANDONED";
        case AuditOutcome::Started: return "STARTED";
    }
    return "FAILED";
}

std::string AuditLog::append(const std::string& userId, const AuditEntryInput& entry) {
    if (!db_.inTransaction()) {
        throw DbError(0, "audit entries must be written inside the transaction of the change");
    }
    std::optional<std::string> fields;
    if (!entry.changedFields.empty()) {
        std::string joined;
        for (const auto& f : entry.changedFields) {
            if (!joined.empty()) joined += ",";
            joined += f;
        }
        fields = joined;
    }
    const std::string eventId = newEventId();
    Statement st = db_.prepare(
        "INSERT INTO audit_entry (event_id, user_id, os_user, recorded_at, action, record_id, outcome, "
        "changed_fields, reason_code, related_event_id) VALUES (?1, ?2, NULL, ?3, ?4, ?5, ?6, ?7, ?8, ?9)");
    st.bind(1, eventId)
        .bind(2, userId)
        .bind(3, toIsoUtc(clock_.now()))
        .bind(4, entry.action)
        .bind(5, entry.recordId)
        .bind(6, to_string(entry.outcome))
        .bind(7, fields)
        .bind(8, entry.reasonCode)
        .bind(9, entry.relatedEventId);
    st.run();
    return eventId;
}

std::vector<AuditEntry> AuditLog::recent(std::size_t limit) const {
    Statement st = db_.prepare(
        "SELECT event_id, user_id, os_user, recorded_at, action, record_id, outcome, changed_fields, reason_code, "
        "related_event_id FROM audit_entry ORDER BY rowid DESC LIMIT ?1");
    st.bind(1, static_cast<std::int64_t>(limit));
    std::vector<AuditEntry> out;
    while (st.step()) {
        AuditEntry e;
        e.eventId = st.getText(0);
        e.userId = st.getText(1);
        e.osUser = st.getOptionalText(2);
        e.recordedAt = st.getText(3);
        e.action = st.getText(4);
        e.recordId = st.getOptionalText(5);
        e.outcome = st.getText(6);
        e.changedFields = st.getOptionalText(7);
        e.reasonCode = st.getOptionalText(8);
        e.relatedEventId = st.getOptionalText(9);
        out.push_back(std::move(e));
    }
    return out;
}

}  // namespace hms
