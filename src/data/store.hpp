#pragma once
// Opening the data store (first part of HMS-F-053, start-up states).
//
// Implemented now:
//   - a missing store is created only after the user confirms (rule 1);
//   - a store that fails the integrity check, or is not an HMS store, is
//     refused and never modified (rule 2);
//   - a store with a newer schema version than this build is refused (12);
//   - an older schema version is migrated forward, one step at a time.
// Later: installation marker, first-run Admin setup, no-Admin rule,
// Recovery mode, backup and restore.

#include <memory>
#include <string>

#include "data/database.hpp"

namespace hms {

enum class StoreStatus {
    Opened,
    NotFound,            // no store file and creation not allowed
    Damaged,             // integrity check failed, unreadable, or not an HMS store
    UnsupportedVersion,  // schema version newer than this build
    Failed               // could not create or open for another reason
};

struct StoreOpenResult {
    StoreStatus status = StoreStatus::Failed;
    std::string message;               // explanation for the user
    std::unique_ptr<Database> db;      // set only when status == Opened
    int schemaVersion = 0;
    bool created = false;              // a new, empty store was created
    bool migrated = false;             // an older schema was brought forward
};

bool storeFileExists(const std::string& utf8Path);

// The caller must already hold the StoreLock for this path.
StoreOpenResult openStore(const std::string& utf8Path, bool createIfMissing);

// Integrity check (SRS 1.4 checks (1) and (2)): storage engine check plus
// foreign-key check. Returns an empty string when healthy.
std::string checkIntegrity(Database& db);

}  // namespace hms
