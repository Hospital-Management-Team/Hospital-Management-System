#include "data/schema.hpp"

namespace hms::schema {

namespace {

// Version 1 - issue #2: patients, audit trail, duplicate-review decisions.
// Field names follow the SAD v11 draft data dictionary (4.3.2) in snake_case.
// Store-enforced invariants (SRS v11 draft 2.5): CHECK constraints, unique keys,
// foreign keys, and an append-only audit table.
const char* kV1 = R"SQL(
CREATE TABLE patient (
    uhid             INTEGER PRIMARY KEY AUTOINCREMENT,      -- store counter, never reused
    full_name        TEXT    NOT NULL CHECK (length(full_name) >= 1),
    normalised_name  TEXT    NOT NULL CHECK (length(normalised_name) >= 1),
    date_of_birth    TEXT    NOT NULL
                     CHECK (date_of_birth GLOB '[0-9][0-9][0-9][0-9]-[0-9][0-9]-[0-9][0-9]'),
    dob_estimated    INTEGER NOT NULL DEFAULT 0 CHECK (dob_estimated IN (0, 1)),
    gender           TEXT    NOT NULL CHECK (gender IN ('Male', 'Female', 'Other')),
    address          TEXT    NOT NULL CHECK (length(address) >= 1),
    phone            TEXT    CHECK (phone IS NULL OR
                                    phone GLOB '[6-9][0-9][0-9][0-9][0-9][0-9][0-9][0-9][0-9][0-9]')
);
-- Strong-duplicate lookup (normalised name + date of birth + gender).
CREATE INDEX idx_patient_identity ON patient (normalised_name, date_of_birth, gender);
-- Phone lookup. Deliberately NOT unique: family members may share a number.
CREATE INDEX idx_patient_phone ON patient (phone);

CREATE TABLE audit_entry (
    event_id          TEXT PRIMARY KEY,                      -- random 128-bit value
    user_id           TEXT NOT NULL,
    os_user           TEXT,
    recorded_at       TEXT NOT NULL,                         -- UTC, ISO 8601
    action            TEXT NOT NULL,
    record_id         TEXT,                                  -- an ID, never patient data
    outcome           TEXT NOT NULL CHECK (outcome IN
                        ('OK','DENIED','FAILED','PREPARED','PUBLISHED','ABANDONED','STARTED')),
    changed_fields    TEXT,                                  -- field names only
    reason_code       TEXT,
    related_event_id  TEXT REFERENCES audit_entry (event_id)
);
CREATE INDEX idx_audit_recorded ON audit_entry (recorded_at);

-- HMS-SR-004: no function changes or deletes an audit entry. Enforced by the store.
CREATE TRIGGER audit_entry_no_update BEFORE UPDATE ON audit_entry
BEGIN SELECT RAISE(ABORT, 'audit entries are read-only'); END;
CREATE TRIGGER audit_entry_no_delete BEFORE DELETE ON audit_entry
BEGIN SELECT RAISE(ABORT, 'audit entries are read-only'); END;

-- HMS-F-002(4),(9): every duplicate decision is stored with the reviewed set.
-- (Not in the SAD data dictionary yet; listed in docs/deviation-log.md.)
CREATE TABLE duplicate_review (
    event_id      TEXT PRIMARY KEY REFERENCES audit_entry (event_id),
    decision      TEXT NOT NULL CHECK (decision IN ('USE_EXISTING', 'DIFFERENT_PERSON')),
    chosen_uhid   INTEGER REFERENCES patient (uhid),         -- USE_EXISTING only
    created_uhid  INTEGER REFERENCES patient (uhid),         -- DIFFERENT_PERSON only
    CHECK ((decision = 'USE_EXISTING' AND chosen_uhid IS NOT NULL AND created_uhid IS NULL) OR
           (decision = 'DIFFERENT_PERSON' AND created_uhid IS NOT NULL AND chosen_uhid IS NULL))
);
CREATE TABLE duplicate_review_candidate (
    event_id  TEXT    NOT NULL REFERENCES duplicate_review (event_id),
    uhid      INTEGER NOT NULL REFERENCES patient (uhid),
    kind      TEXT    NOT NULL CHECK (kind IN ('STRONG', 'SAME_PHONE')),
    PRIMARY KEY (event_id, uhid)
);
)SQL";
// Version 2 - issue #3: staff accounts for login.
const char* kV2 = R"SQL(
CREATE TABLE staff_accounts (
    staff_id        INTEGER PRIMARY KEY AUTOINCREMENT,
    username        TEXT NOT NULL COLLATE NOCASE UNIQUE,
    password_hash   TEXT NOT NULL,
    role            TEXT NOT NULL
                    CHECK (role IN ('administrator', 'doctor', 'receptionist')),
    failed_attempts INTEGER NOT NULL DEFAULT 0 CHECK (failed_attempts >= 0),
    locked          INTEGER NOT NULL DEFAULT 0 CHECK (locked IN (0, 1)),
    created_at      TEXT NOT NULL DEFAULT CURRENT_TIMESTAMP
);
)SQL";
}  // namespace

// Each feature appends its tables as the next numbered migration.
// Rules: never edit a migration after it is merged; append a new one instead.
const std::vector<Migration>& migrations() {
    static const std::vector<Migration> kAll = {
        {1, "Patients, audit trail, duplicate reviews (#2)", kV1},
        {2, "Staff accounts and authentication (#3)", kV2},
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
