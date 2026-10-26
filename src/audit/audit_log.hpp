#pragma once
// Audit Module (SRS HMS-SR-004; SAD 3.4).
//
// Rules implemented:
//  - every entry has a unique random event ID, user ID, UTC timestamp,
//    action, affected record ID and outcome;
//  - an entry for a data change is written in the SAME transaction as the
//    change: append() must be called inside the caller's Transaction, so if
//    the entry cannot be written the change is rolled back with it;
//  - entries hold IDs and field names only - never names, phone numbers,
//    clinical text or free text typed by staff;
//  - no function updates or deletes an entry (the store refuses it too).

#include <optional>
#include <string>
#include <vector>

#include "core/clock.hpp"
#include "data/database.hpp"

namespace hms {

enum class AuditOutcome { Ok, Denied, Failed, Prepared, Published, Abandoned, Started };
std::string to_string(AuditOutcome outcome);

// Action names. One value per audited action; add new ones per sprint.
namespace audit_action {
inline constexpr const char* kPatientRegister = "PATIENT_REGISTER";
inline constexpr const char* kPatientUseExisting = "PATIENT_DUPLICATE_USE_EXISTING";
inline constexpr const char* kPatientUpdateDemographics = "PATIENT_UPDATE_DEMOGRAPHICS";
inline constexpr const char* kPatientSearch = "PATIENT_SEARCH";
inline constexpr const char* kPatientView = "PATIENT_VIEW";
}  // namespace audit_action

struct AuditEntryInput {
    std::string action;
    std::optional<std::string> recordId;
    AuditOutcome outcome = AuditOutcome::Ok;
    std::vector<std::string> changedFields;  // field names only
    std::optional<std::string> reasonCode;
    std::optional<std::string> relatedEventId;
};

struct AuditEntry {
    std::string eventId;
    std::string userId;
    std::optional<std::string> osUser;
    std::string recordedAt;
    std::string action;
    std::optional<std::string> recordId;
    std::string outcome;
    std::optional<std::string> changedFields;
    std::optional<std::string> reasonCode;
    std::optional<std::string> relatedEventId;
};

class AuditLog {
public:
    AuditLog(Database& db, const Clock& clock) : db_(db), clock_(clock) {}

    // Appends one entry and returns its event ID. Call inside a Transaction.
    std::string append(const std::string& userId, const AuditEntryInput& entry);

    // Newest first (read-only view; the Admin screen comes later).
    std::vector<AuditEntry> recent(std::size_t limit) const;

private:
    Database& db_;
    const Clock& clock_;
};

}  // namespace hms
