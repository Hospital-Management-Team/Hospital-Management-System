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

// Version 3 - mobile number required unless staff confirm "no phone available"
// (deviation D-04), and the name + date-of-birth lookup for returning patients.
// Migration 1 is merged and is not edited: patient.phone stays nullable and the
// rule is added with a flag and triggers. The triggers check new and changed
// rows only, so records saved before this version (phone NULL, flag 0) stay
// valid and are shown as "not recorded".
const char* kV3 = R"SQL(
ALTER TABLE patient ADD COLUMN no_phone_confirmed INTEGER NOT NULL DEFAULT 0
    CHECK (no_phone_confirmed IN (0, 1));

CREATE TRIGGER patient_phone_rule_insert BEFORE INSERT ON patient
WHEN (NEW.phone IS NULL) = (NEW.no_phone_confirmed = 0)
BEGIN SELECT RAISE(ABORT, 'a patient needs a mobile number or a confirmed "no phone available"'); END;

CREATE TRIGGER patient_phone_rule_update BEFORE UPDATE OF phone, no_phone_confirmed ON patient
WHEN (NEW.phone IS NULL) = (NEW.no_phone_confirmed = 0)
BEGIN SELECT RAISE(ABORT, 'a patient needs a mobile number or a confirmed "no phone available"'); END;

-- Returning-patient lookup by name and date of birth.
CREATE INDEX idx_patient_dob ON patient (date_of_birth);
)SQL";

// Version 4 - issue #4: doctor schedules (HMS-F-020, minimal), appointments
// (HMS-F-010..F-012) and reminder entries (HMS-F-013).
// Times of day are hospital-local minutes after midnight; slot_start is the
// hospital-local start "YYYY-MM-DDTHH:MM", which sorts in time order.
const char* kV4 = R"SQL(
CREATE TABLE doctor_profile (
    staff_id        INTEGER PRIMARY KEY REFERENCES staff_accounts (staff_id),
    department      TEXT    NOT NULL CHECK (length(department) BETWEEN 1 AND 60),
    specialization  TEXT    CHECK (specialization IS NULL OR length(specialization) BETWEEN 1 AND 60),
    updated_at      TEXT    NOT NULL
);
-- A profile can only belong to a Doctor account.
CREATE TRIGGER doctor_profile_role_insert BEFORE INSERT ON doctor_profile
WHEN (SELECT role FROM staff_accounts WHERE staff_id = NEW.staff_id) IS NOT 'doctor'
BEGIN SELECT RAISE(ABORT, 'a doctor profile needs a doctor account'); END;

-- One working window per weekday (1 = Monday ... 7 = Sunday).
CREATE TABLE doctor_availability (
    staff_id      INTEGER NOT NULL REFERENCES doctor_profile (staff_id),
    weekday       INTEGER NOT NULL CHECK (weekday BETWEEN 1 AND 7),
    start_minute  INTEGER NOT NULL CHECK (start_minute BETWEEN 0 AND 1439),
    end_minute    INTEGER NOT NULL CHECK (end_minute BETWEEN 1 AND 1440),
    CHECK (end_minute > start_minute),
    PRIMARY KEY (staff_id, weekday)
);

CREATE TABLE appointment (
    appointment_id   INTEGER PRIMARY KEY AUTOINCREMENT,      -- store counter, never reused
    uhid             INTEGER NOT NULL REFERENCES patient (uhid),
    doctor_staff_id  INTEGER NOT NULL REFERENCES doctor_profile (staff_id),
    slot_start       TEXT    NOT NULL
                     CHECK (slot_start GLOB '[0-9][0-9][0-9][0-9]-[0-9][0-9]-[0-9][0-9]T[0-9][0-9]:[0-9][0-9]'),
    slot_minutes     INTEGER NOT NULL CHECK (slot_minutes BETWEEN 5 AND 240),
    status           TEXT    NOT NULL CHECK (status IN ('BOOKED', 'CANCELLED', 'COMPLETED', 'NO_SHOW')),
    replaced_by      INTEGER REFERENCES appointment (appointment_id),  -- set when rescheduled
    created_at       TEXT    NOT NULL,                       -- UTC, ISO 8601
    created_by       TEXT    NOT NULL,
    cancelled_at     TEXT,
    cancelled_by     TEXT,
    CHECK ((status = 'CANCELLED') = (cancelled_at IS NOT NULL AND cancelled_by IS NOT NULL)),
    CHECK (replaced_by IS NULL OR status = 'CANCELLED')
);
-- HMS-F-011, enforced by the store: one active appointment per doctor per
-- slot start, and a patient cannot hold two active appointments at the same
-- start. (Overlaps between different starts are refused by the service.)
CREATE UNIQUE INDEX ux_appointment_doctor_slot ON appointment (doctor_staff_id, slot_start)
    WHERE status = 'BOOKED';
CREATE UNIQUE INDEX ux_appointment_patient_slot ON appointment (uhid, slot_start)
    WHERE status = 'BOOKED';
CREATE INDEX idx_appointment_slot ON appointment (slot_start);

-- HMS-F-013: one reminder entry per appointment.
CREATE TABLE appointment_reminder (
    appointment_id  INTEGER PRIMARY KEY REFERENCES appointment (appointment_id),
    generated_at    TEXT    NOT NULL,                        -- UTC, ISO 8601
    generated_by    TEXT    NOT NULL
);
)SQL";

// Version 5 - issue #5: consultations (HMS-F-022). A consultation belongs to one
// appointment, one patient and one doctor, and holds the diagnosis, the
// prescription lines and the recommended laboratory tests. Medicine and test
// names are free text here; the laboratory and pharmacy features (#8) link
// results and dispensing to these rows.
const char* kV5 = R"SQL(
CREATE TABLE consultation (
    consultation_id  INTEGER PRIMARY KEY AUTOINCREMENT,
    appointment_id   INTEGER NOT NULL UNIQUE REFERENCES appointment (appointment_id),
    uhid             INTEGER NOT NULL REFERENCES patient (uhid),
    doctor_staff_id  INTEGER NOT NULL REFERENCES doctor_profile (staff_id),
    diagnosis        TEXT    NOT NULL CHECK (length(diagnosis) BETWEEN 1 AND 1000),
    recorded_by      TEXT    NOT NULL,
    recorded_at      TEXT    NOT NULL                        -- UTC, ISO 8601
);
CREATE INDEX idx_consultation_patient ON consultation (uhid, consultation_id);

-- The store itself refuses a consultation whose patient or doctor differ from its appointment.
CREATE TRIGGER consultation_matches_appointment BEFORE INSERT ON consultation
WHEN NOT EXISTS (SELECT 1 FROM appointment a WHERE a.appointment_id = NEW.appointment_id
                 AND a.uhid = NEW.uhid AND a.doctor_staff_id = NEW.doctor_staff_id)
BEGIN SELECT RAISE(ABORT, 'a consultation must match its appointment'); END;

CREATE TABLE prescription_item (
    item_id          INTEGER PRIMARY KEY AUTOINCREMENT,
    consultation_id  INTEGER NOT NULL REFERENCES consultation (consultation_id),
    medicine_name    TEXT    NOT NULL CHECK (length(medicine_name) BETWEEN 1 AND 100),
    dosage           TEXT    NOT NULL CHECK (length(dosage) BETWEEN 1 AND 100),
    quantity         INTEGER NOT NULL CHECK (quantity BETWEEN 1 AND 1000)
);
CREATE INDEX idx_prescription_consultation ON prescription_item (consultation_id);

CREATE TABLE lab_order (
    order_id         INTEGER PRIMARY KEY AUTOINCREMENT,
    consultation_id  INTEGER NOT NULL REFERENCES consultation (consultation_id),
    test_name        TEXT    NOT NULL CHECK (length(test_name) BETWEEN 1 AND 100),
    instructions     TEXT    CHECK (instructions IS NULL OR length(instructions) <= 200),
    ordered_at       TEXT    NOT NULL                        -- UTC, ISO 8601
);
CREATE INDEX idx_lab_order_consultation ON lab_order (consultation_id);
)SQL";
}  // namespace

// Each feature appends its tables as the next numbered migration.
// Rules: never edit a migration after it is merged; append a new one instead.
const std::vector<Migration>& migrations() {
    static const std::vector<Migration> kAll = {
        {1, "Patients, audit trail, duplicate reviews (#2)", kV1},
        {2, "Staff accounts and authentication (#3)", kV2},
        {3, "Mobile number or confirmed no phone; date-of-birth lookup (#2 follow-up)", kV3},
        {4, "Doctor schedules, appointments and reminders (#4)", kV4},
        {5, "Consultations: diagnosis, prescriptions, test orders (#5)", kV5},
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
