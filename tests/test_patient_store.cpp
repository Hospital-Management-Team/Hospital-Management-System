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
    CHECK(f.count("PRAGMA user_version") == 1);
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
    // A phone number may be shared (no unique constraint) and may be absent.
    f.db().exec("INSERT INTO patient (full_name, normalised_name, date_of_birth, gender, address, phone) "
                "VALUES ('A', 'a', '1990-01-01', 'Male', 'x', '9876543210'), "
                "('B', 'b', '1991-01-01', 'Female', 'x', '9876543210'), "
                "('C', 'c', '1992-01-01', 'Other', 'x', NULL)");
    CHECK(f.count("SELECT count(*) FROM patient") == 3);
}
