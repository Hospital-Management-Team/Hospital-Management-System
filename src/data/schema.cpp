
#include "data/schema.hpp"

namespace hms::schema {

const std::vector<Migration>& migrations() {
    static const std::vector<Migration> kAll = {
        {
            1,
            "Staff accounts and authentication (#3)",
            R"sql(
                CREATE TABLE staff_accounts (
                    staff_id INTEGER PRIMARY KEY AUTOINCREMENT,
                    username TEXT NOT NULL COLLATE NOCASE UNIQUE,
                    password_hash TEXT NOT NULL,
                    role TEXT NOT NULL
                        CHECK (role IN (
                            'administrator',
                            'doctor',
                            'receptionist'
                        )),
                    failed_attempts INTEGER NOT NULL DEFAULT 0
                        CHECK (failed_attempts >= 0),
                    locked INTEGER NOT NULL DEFAULT 0
                        CHECK (locked IN (0, 1)),
                    created_at TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
                );
            )sql"
        }
    };

    return kAll;
}

int currentVersion() {
    return migrations().empty() ? 0 : migrations().back().version;
}

void migrate(Database& db, int fromVersion) {
    for (const auto& migration : migrations()) {
        if (migration.version <= fromVersion) {
            continue;
        }

        Transaction tx(db);
        db.exec(migration.sql);
        db.exec("PRAGMA user_version = " +
                std::to_string(migration.version));
        tx.commit();
    }
}

}  // namespace hms::schema

