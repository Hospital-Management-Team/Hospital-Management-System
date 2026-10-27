// Store rules added by issue #2: the patient and audit tables, the
// append-only audit trail, store-level constraints, persistence of patients.
#include <doctest.h>

#include "audit/audit_log.hpp"
#include "data/schema.hpp"
#include "patient_test_support.hpp"

using namespace hms;
namespace fs = std::filesystem;

TEST_CASE("a new store has the patient, audit and duplicate-review tables") {
    test::StoreFixture f;
    CHECK(f.count("PRAGMA user_version") >= 1);
    for (const char* t : {"patient", "audit_entry", "duplicate_review", "duplicate_review_candidate"}) {
        CAPTURE(t);
        CHECK(f.count(std::string("SELECT count(*) FROM sqlite_master WHERE type='table' AND name='") + t + "'") == 1);
    }
}

TEST_CASE("an empty store created by the foundation build is upgraded on opening") {
    auto dir = test::uniqueTempDir();
    const auto path = (dir / "old.db").string();
    { Database db(path, Database::Mode::ReadWriteCreate); db.exec("PRAGMA user_version = 0"); }
    std::string err;
    auto lock = StoreLock::acquire(canonicalStorePath(path), err);
    auto s = openStore(path, false);
    REQUIRE(s.status == StoreStatus::Opened);
    CHECK(s.schemaVersion == schema::currentVersion());
    s.db.reset();
    lock.reset();
    fs::remove_all(dir);
}

TEST_CASE("a registered patient is there after a restart") {
    auto dir = test::uniqueTempDir();
    const auto path = (dir / "persist.db").string();
    std::int64_t uhid = 0;
    {
        std::string err;
        auto lock = StoreLock::acquire(canonicalStorePath(path), err);
        auto s = openStore(path, true);
        FixedClock clock(test::fixedNow());
        PatientService svc(*s.db, clock, AppPolicy{});
        auto r = svc.registerPatient(test::receptionist(), test::draft("Ravi Kumar", "05-03-1990", "M"),
                                     DuplicateDecision::none());
        REQUIRE(r.ok());
        uhid = r.value().uhid;
    }
    {
        std::string err;
        auto lock = StoreLock::acquire(canonicalStorePath(path), err);
        auto s = openStore(path, false);
        REQUIRE(s.status == StoreStatus::Opened);
        FixedClock clock(test::fixedNow());
        PatientService svc(*s.db, clock, AppPolicy{});
        auto r = svc.getPatient(test::receptionist(), uhid);
        REQUIRE(r.ok());
        CHECK(r.value().fullName == "Ravi Kumar");
    }
    fs::remove_all(dir);
}

TEST_CASE("HMS-SR-004 audit entries cannot be changed or deleted, even with direct SQL") {
    test::StoreFixture f;
    auto r = f.service->registerPatient(test::receptionist(), test::draft("Ravi Kumar", "05-03-1990", "M"),
                                        DuplicateDecision::none());
    REQUIRE(r.ok());
    CHECK_THROWS_AS(f.db().exec("UPDATE audit_entry SET user_id = 'someone-else'"), DbError);
    CHECK_THROWS_AS(f.db().exec("DELETE FROM audit_entry"), DbError);
    CHECK(f.count("SELECT count(*) FROM audit_entry") == 1);
}

TEST_CASE("audit entries can only be written inside the transaction of the change") {
    test::StoreFixture f;
    AuditLog log(f.db(), f.clock);
    AuditEntryInput e;
    e.action = "TEST";
    CHECK_THROWS_AS(log.append("X", e), DbError);
}

TEST_CASE("store constraints back up the business rules") {
    test::StoreFixture f;
    CHECK_THROWS_AS(f.db().exec("INSERT INTO patient (full_name, normalised_name, date_of_birth, gender, address) "
                                "VALUES ('A', 'a', '1990-01-01', 'Unknown', 'x')"),
                    DbError);
    CHECK_THROWS_AS(f.db().exec("INSERT INTO patient (full_name, normalised_name, date_of_birth, gender, address, "
                                "phone) VALUES ('A', 'a', '1990-01-01', 'Male', 'x', '12345')"),
                    DbError);
    // A phone number may be shared (no unique constraint).
    f.db().exec("INSERT INTO patient (full_name, normalised_name, date_of_birth, gender, address, phone) "
                "VALUES ('A', 'a', '1990-01-01', 'Male', 'x', '9876543210'), "
                "('B', 'b', '1991-01-01', 'Female', 'x', '9876543210')");
    CHECK(f.count("SELECT count(*) FROM patient") == 2);
}

TEST_CASE("D-04 the store requires a mobile number or a confirmed \"no phone available\"") {
    test::StoreFixture f;
    const std::string cols = "INSERT INTO patient (full_name, normalised_name, date_of_birth, gender, address, ";
    // Neither a number nor the confirmation: refused.
    CHECK_THROWS_AS(f.db().exec(cols + "phone) VALUES ('A', 'a', '1990-01-01', 'Male', 'x', NULL)"), DbError);
    CHECK_THROWS_AS(f.db().exec("INSERT INTO patient (full_name, normalised_name, date_of_birth, gender, address) "
                                "VALUES ('A', 'a', '1990-01-01', 'Male', 'x')"),
                    DbError);
    // Both a number and "no phone": refused.
    CHECK_THROWS_AS(f.db().exec(cols + "phone, no_phone_confirmed) VALUES ('A', 'a', '1990-01-01', 'Male', 'x', "
                                       "'9876543210', 1)"),
                    DbError);
    // Either one alone: accepted.
    f.db().exec(cols + "phone) VALUES ('A', 'a', '1990-01-01', 'Male', 'x', '9876543210')");
    f.db().exec(cols + "phone, no_phone_confirmed) VALUES ('B', 'b', '1990-01-01', 'Male', 'x', NULL, 1)");
    CHECK(f.count("SELECT count(*) FROM patient") == 2);
    // Removing the number later without the confirmation: refused.
    CHECK_THROWS_AS(f.db().exec("UPDATE patient SET phone = NULL WHERE full_name = 'A'"), DbError);
}

TEST_CASE("D-04 a version-2 store with a patient who has no phone upgrades and keeps the record") {
    auto dir = test::uniqueTempDir();
    const auto path = (dir / "v2.db").string();
    {
        // A store as Sprint 1 left it: migrations 1 and 2, a patient without a phone.
        Database db(path, Database::Mode::ReadWriteCreate);
        for (const auto& m : schema::migrations()) {
            if (m.version > 2) break;
            db.exec(m.sql);
        }
        db.exec("PRAGMA user_version = 2");
        db.exec("INSERT INTO patient (full_name, normalised_name, date_of_birth, gender, address, phone) "
                "VALUES ('Old Record', 'old record', '1980-01-01', 'Male', 'x', NULL)");
    }
    {
        auto r = openStore(path, false);
        REQUIRE(r.status == StoreStatus::Opened);
        CHECK(r.migrated);
        CHECK(r.db->queryInt("PRAGMA user_version").value_or(0) == schema::currentVersion());
        CHECK(r.db->queryInt("SELECT count(*) FROM patient WHERE phone IS NULL AND no_phone_confirmed = 0")
                  .value_or(-1) == 1);
        // The old record can still be read through the repository.
        PatientRepository repo(*r.db);
        auto rec = repo.findByUhid(1);
        REQUIRE(rec.has_value());
        CHECK_FALSE(rec->phone.has_value());
        CHECK_FALSE(rec->noPhoneConfirmed);
    }
    fs::remove_all(dir);
}
