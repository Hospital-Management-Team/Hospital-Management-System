#include "data/schema.hpp"

namespace hms::schema {

// Each feature appends its tables as the next numbered migration.
// Rules: never edit a migration after it is merged; append a new one instead.
const std::vector<Migration>& migrations() {
    static const std::vector<Migration> kAll = {
        // {1, "Patients and audit trail (#2)", kV1},   <- features append here
    };
    return kAll;
}

int currentVersion() { return migrations().empty() ? 0 : migrations().back().version; }

void migrate(Database& db, int fromVersion) {
    for (const auto& m : migrations()) {
        if (m.version <= fromVersion) continue;
        Transaction tx(db);
        db.exec(m.sql);
        db.exec("PRAGMA user_version = " + std::to_string(m.version));
        tx.commit();
    }
}

}  // namespace hms::schema
