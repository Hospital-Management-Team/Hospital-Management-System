// TC-Lab-01 (lab result entry and update, HMS-F-040) for issue #8, and the
// migration that adds the Lab Technician and Pharmacist roles (deviation E-07).
// Integration tests against a real SQLite store.
#include <doctest.h>

#include <string>

#include "data/schema.hpp"
#include "lab_pharmacy_test_support.hpp"

using namespace hms;
using test::at;
using test::doctorSession;
using test::kMonday;
using test::kTuesday;
using test::LabPharmacyFixture;
using test::labTech;
using test::pharmacistSession;
using test::receptionist;

TEST_CASE("TC-Lab-01 a result is linked to the ordered test and the treating doctor can see it") {
    LabPharmacyFixture f;
    const auto doc = f.addDoctor("mehta");
    const auto uhid = f.addPatient("Ravi Kumar");
    const auto c = f.seen(uhid, doc, "mehta", {"Complete Blood Count", "Chest X-ray"});
    REQUIRE(c.labOrders.size() == 2);

    auto pending = f.lab.listPendingOrders(labTech());
    REQUIRE(pending.ok());
    REQUIRE(pending.value().size() == 2);
    CHECK(pending.value()[0].testName == "Complete Blood Count");
    CHECK(pending.value()[0].patientName == "Ravi Kumar");
    CHECK_FALSE(pending.value()[0].hasResult());

    const auto order = c.labOrders[0].orderId;
    auto saved = f.lab.recordResult(labTech(), order, "Hb 13.2 g/dL, WBC 7,400");
    REQUIRE(saved.ok());
    CHECK(saved.value().orderId == order);
    CHECK(saved.value().latest->text == "Hb 13.2 g/dL, WBC 7,400");
    CHECK(saved.value().latest->revision == 1);
    CHECK(saved.value().latest->enteredBy == "lab1");
    CHECK(f.count("SELECT count(*) FROM lab_result WHERE order_id = " + std::to_string(order)) == 1);

    // Only the other test is still waiting.
    auto left = f.lab.listPendingOrders(labTech());
    REQUIRE(left.ok());
    REQUIRE(left.value().size() == 1);
    CHECK(left.value()[0].testName == "Chest X-ray");

    // The treating doctor sees the result next to the order.
    auto seen = f.lab.listResultsForPatient(doctorSession("mehta"), uhid);
    REQUIRE(seen.ok());
    REQUIRE(seen.value().size() == 2);
    int withResult = 0;
    for (const auto& o : seen.value()) {
        if (o.hasResult()) {
            ++withResult;
            CHECK(o.testName == "Complete Blood Count");
            CHECK(o.latest->text == "Hb 13.2 g/dL, WBC 7,400");
        }
    }
    CHECK(withResult == 1);
}

TEST_CASE("TC-Lab-01 updating a result keeps every earlier revision") {
    LabPharmacyFixture f;
    const auto doc = f.addDoctor("mehta");
    const auto c = f.seen(f.addPatient("Ravi Kumar"), doc, "mehta", {"Blood Sugar"});
    const auto order = c.labOrders[0].orderId;

    REQUIRE(f.lab.recordResult(labTech(), order, "Fasting 118 mg/dL").ok());
    auto same = f.lab.recordResult(labTech(), order, "Fasting 118 mg/dL");  // nothing new to save
    REQUIRE_FALSE(same.ok());
    CHECK(same.error().kind == ErrorKind::Conflict);

    auto updated = f.lab.recordResult(labTech("lab2"), order, "Fasting 112 mg/dL (repeat sample)");
    REQUIRE(updated.ok());
    CHECK(updated.value().latest->revision == 2);
    CHECK(updated.value().revisionCount == 2);

    auto history = f.lab.getResultHistory(labTech(), order);
    REQUIRE(history.ok());
    REQUIRE(history.value().size() == 2);
    CHECK(history.value()[0].text == "Fasting 118 mg/dL");
    CHECK(history.value()[1].text == "Fasting 112 mg/dL (repeat sample)");
    CHECK(history.value()[1].enteredBy == "lab2");
    CHECK(f.count("SELECT count(*) FROM lab_order") == 1);  // still one order
}

TEST_CASE("TC-Lab-01 the doctor sees only the tests that doctor ordered") {
    LabPharmacyFixture f;
    const auto mehta = f.addDoctor("mehta");
    const auto rao = f.addDoctor("rao");
    const auto uhid = f.addPatient("Ravi Kumar");
    f.seen(uhid, mehta, "mehta", {"CBC"});
    f.seen(uhid, rao, "rao", {"Lipid Profile"}, {}, at(kTuesday, 9, 0));

    auto m = f.lab.listResultsForPatient(doctorSession("mehta"), uhid);
    REQUIRE(m.ok());
    REQUIRE(m.value().size() == 1);
    CHECK(m.value()[0].testName == "CBC");
    auto r = f.lab.listResultsForPatient(doctorSession("rao"), uhid);
    REQUIRE(r.ok());
    REQUIRE(r.value().size() == 1);
    CHECK(r.value()[0].testName == "Lipid Profile");

    // The lab technician finds both for the patient.
    auto all = f.lab.listOrdersForPatient(labTech(), uhid);
    REQUIRE(all.ok());
    CHECK(all.value().size() == 2);
    auto none = f.lab.listOrdersForPatient(labTech(), 999);
    REQUIRE_FALSE(none.ok());
    CHECK(none.error().kind == ErrorKind::NotFound);
}

TEST_CASE("TC-Lab-01 an unknown order or an invalid result is refused and nothing is saved") {
    LabPharmacyFixture f;
    const auto doc = f.addDoctor("mehta");
    const auto c = f.seen(f.addPatient("Ravi Kumar"), doc, "mehta", {"CBC"});
    const auto order = c.labOrders[0].orderId;

    auto missing = f.lab.recordResult(labTech(), 99, "Normal");
    REQUIRE_FALSE(missing.ok());
    CHECK(missing.error().kind == ErrorKind::NotFound);

    auto blank = f.lab.recordResult(labTech(), order, "   ");
    REQUIRE_FALSE(blank.ok());
    CHECK(blank.error().kind == ErrorKind::Validation);

    auto longText = f.lab.recordResult(labTech(), order, std::string(501, 'x'));
    REQUIRE_FALSE(longText.ok());
    CHECK(longText.error().kind == ErrorKind::Validation);
    CHECK(f.lab.recordResult(labTech(), order, std::string(500, 'x')).ok());  // the limit itself is allowed

    CHECK(f.count("SELECT count(*) FROM lab_result") == 1);
    CHECK(parseLabOrderId("LAB-000012") == 12);
    CHECK(parseLabOrderId("lab-12") == 12);
    CHECK(parseLabOrderId("12") == 12);
    CHECK_FALSE(parseLabOrderId("LAB-").has_value());
    CHECK_FALSE(parseLabOrderId("0").has_value());
    CHECK_FALSE(parseLabOrderId("12a").has_value());
    CHECK(formatLabOrderId(12) == "LAB-000012");
}

TEST_CASE("TC-Lab-01 only the Lab Technician enters results; the doctor reads only own patients; denials are audited") {
    LabPharmacyFixture f;
    const auto doc = f.addDoctor("mehta");
    const auto uhid = f.addPatient("Ravi Kumar");
    const auto c = f.seen(uhid, doc, "mehta", {"CBC"});
    const auto order = c.labOrders[0].orderId;

    for (const Session& s : {receptionist(), doctorSession("mehta"), pharmacistSession()}) {
        auto r = f.lab.recordResult(s, order, "Normal");
        REQUIRE_FALSE(r.ok());
        CHECK(r.error().kind == ErrorKind::AccessDenied);
    }
    CHECK(f.count("SELECT count(*) FROM lab_result") == 0);
    CHECK(f.count("SELECT count(*) FROM audit_entry WHERE action = 'LAB_RESULT_RECORD' AND outcome = 'DENIED'") == 3);

    CHECK(f.lab.listPendingOrders(receptionist()).error().kind == ErrorKind::AccessDenied);
    CHECK(f.lab.listPendingOrders(doctorSession("mehta")).error().kind == ErrorKind::AccessDenied);
    CHECK(f.lab.getOrder(pharmacistSession(), order).error().kind == ErrorKind::AccessDenied);
    CHECK(f.lab.listResultsForPatient(labTech(), uhid).error().kind == ErrorKind::AccessDenied);
    CHECK(f.lab.listResultsForPatient(receptionist(), uhid).error().kind == ErrorKind::AccessDenied);
    CHECK(f.count("SELECT count(*) FROM audit_entry WHERE action = 'LAB_ORDER_VIEW' AND outcome = 'DENIED'") == 3);
    CHECK(f.count("SELECT count(*) FROM audit_entry WHERE action = 'LAB_RESULT_VIEW' AND outcome = 'DENIED'") == 2);

    // A doctor account that does not exist as a doctor in the store is refused as well.
    auto stranger = f.lab.listResultsForPatient(doctorSession("nobody"), uhid);
    REQUIRE_FALSE(stranger.ok());
    CHECK(stranger.error().kind == ErrorKind::AccessDenied);
}

TEST_CASE("TC-Lab-01 the audit entries hold IDs and field names only") {
    LabPharmacyFixture f;
    const auto doc = f.addDoctor("mehta");
    const auto c = f.seen(f.addPatient("Ravi Kumar"), doc, "mehta", {"Thyroid Panel"});
    REQUIRE(f.lab.recordResult(labTech(), c.labOrders[0].orderId, "TSH 2.4 normal").ok());

    CHECK(f.count("SELECT count(*) FROM audit_entry WHERE action = 'LAB_RESULT_RECORD' AND outcome = 'OK' "
                  "AND record_id = 'LAB-000001' AND user_id = 'lab1'") == 1);
    for (const char* secret : {"Ravi", "Kumar", "Thyroid", "TSH", "normal"}) {
        CHECK(f.count(std::string("SELECT count(*) FROM audit_entry WHERE ifnull(record_id,'') || ifnull(changed_fields,'') "
                                  "|| ifnull(reason_code,'') LIKE '%") + secret + "%'") == 0);
    }
}

TEST_CASE("TC-Lab-01 the store refuses to change or delete a saved result") {
    LabPharmacyFixture f;
    const auto doc = f.addDoctor("mehta");
    const auto c = f.seen(f.addPatient("Ravi Kumar"), doc, "mehta", {"CBC"});
    REQUIRE(f.lab.recordResult(labTech(), c.labOrders[0].orderId, "Normal").ok());

    CHECK_THROWS_AS(f.db().exec("UPDATE lab_result SET result_text = 'Altered'"), DbError);
    CHECK_THROWS_AS(f.db().exec("DELETE FROM lab_result"), DbError);
    CHECK_THROWS_AS(f.db().exec("INSERT INTO lab_result (order_id, revision, result_text, entered_by, entered_at) "
                                "VALUES (1, 1, 'Second revision 1', 'lab1', '2026-10-26T04:00:00Z')"), DbError);
    CHECK(f.count("SELECT count(*) FROM lab_result") == 1);
}

TEST_CASE("TC-Lab-01 NF-002 a failure before commit saves nothing and the retry works") {
    LabPharmacyFixture f;
    const auto doc = f.addDoctor("mehta");
    const auto c = f.seen(f.addPatient("Ravi Kumar"), doc, "mehta", {"CBC"});
    const auto order = c.labOrders[0].orderId;

    f.db().setFaultHook([](const char* point) {
        if (std::string(point) == "lab_result_before_commit") throw InjectedFault(point);
    });
    auto failed = f.lab.recordResult(labTech(), order, "Normal");
    f.db().setFaultHook(nullptr);
    REQUIRE_FALSE(failed.ok());
    CHECK(failed.error().kind == ErrorKind::StorageFailure);
    CHECK(f.count("SELECT count(*) FROM lab_result") == 0);
    CHECK(f.count("SELECT count(*) FROM audit_entry WHERE action = 'LAB_RESULT_RECORD' AND outcome = 'OK'") == 0);
    CHECK(f.lab.recordResult(labTech(), order, "Normal").ok());
}

// ------------------------------------------------------------ migration 8 (deviation E-07)

namespace {
// Applies migrations 1 to 8 to a new file, the way a store made by the previous build looks.
void buildVersion8(Database& db) {
    db.exec("PRAGMA foreign_keys = ON");
    for (const auto& m : schema::migrations()) {
        if (m.version > 8) break;
        Transaction tx(db);
        db.exec(m.sql);
        db.exec("PRAGMA user_version = " + std::to_string(m.version));
        tx.commit();
    }
}
}  // namespace

TEST_CASE("TC-Lab-01 migration 9 keeps every account and doctor profile and accepts the two new roles") {
    const auto dir = test::uniqueTempDir();
    {
        Database db((dir / "old.db").string(), Database::Mode::ReadWriteCreate);
        buildVersion8(db);
        db.exec("INSERT INTO staff_accounts (username, password_hash, role, failed_attempts, locked, locked_until) "
                "VALUES ('admin', 'hash-a', 'administrator', 0, 0, NULL), "
                "('mehta', 'hash-m', 'doctor', 2, 1, '2026-10-25T07:00:00Z'), "
                "('desk', 'hash-r', 'receptionist', 0, 0, NULL)");
        db.exec("INSERT INTO doctor_profile (staff_id, department, specialization, updated_at) "
                "VALUES (2, 'General Medicine', NULL, '2026-10-25T05:00:00Z')");
        db.exec("INSERT INTO patient (full_name, normalised_name, date_of_birth, gender, address, phone) "
                "VALUES ('Ravi Kumar', 'ravi kumar', '1990-03-05', 'Male', 'Pune', '9876543210')");
        db.exec("INSERT INTO medical_history_version (uhid, history, allergies, current_medications, author_staff_id, recorded_at) "
                "VALUES (1, 'None', 'None', 'None', 2, '2026-10-25T05:30:00Z')");
        CHECK(db.queryInt("PRAGMA user_version").value_or(0) == 8);
        CHECK_THROWS_AS(db.exec("INSERT INTO staff_accounts (username, password_hash, role) VALUES ('lab1', 'x', 'lab_technician')"),
                        DbError);  // before: the role is not allowed

        schema::migrate(db, 8);

        CHECK(db.queryInt("PRAGMA user_version").value_or(0) == schema::currentVersion());
        CHECK(db.queryInt("PRAGMA foreign_keys").value_or(0) == 1);  // checking is back on
        CHECK(db.queryInt("SELECT count(*) FROM staff_accounts").value_or(0) == 3);
        CHECK(db.queryInt("SELECT count(*) FROM staff_accounts WHERE username = 'mehta' AND staff_id = 2 AND "
                          "password_hash = 'hash-m' AND role = 'doctor' AND failed_attempts = 2 AND locked = 1 AND "
                          "locked_until = '2026-10-25T07:00:00Z'").value_or(0) == 1);
        CHECK(db.queryInt("SELECT count(*) FROM doctor_profile WHERE staff_id = 2").value_or(0) == 1);
        CHECK(db.queryInt("SELECT count(*) FROM medical_history_version WHERE author_staff_id = 2").value_or(0) == 1);
        CHECK_FALSE(db.prepare("PRAGMA foreign_key_check").step());

        db.exec("INSERT INTO staff_accounts (username, password_hash, role) VALUES ('lab1', 'x', 'lab_technician')");
        db.exec("INSERT INTO staff_accounts (username, password_hash, role) VALUES ('pharm1', 'x', 'pharmacist')");
        CHECK_THROWS_AS(db.exec("INSERT INTO staff_accounts (username, password_hash, role) VALUES ('x1', 'x', 'janitor')"), DbError);
        CHECK_THROWS_AS(db.exec("INSERT INTO staff_accounts (username, password_hash, role) VALUES ('MEHTA', 'x', 'doctor')"),
                        DbError);  // the username stays unique, ignoring case
        // A doctor profile still needs a doctor account.
        CHECK_THROWS_AS(db.exec("INSERT INTO doctor_profile (staff_id, department, updated_at) VALUES (4, 'Lab', 'now')"), DbError);
        // The first new account gets the next number: IDs were not reused or renumbered.
        CHECK(db.queryInt("SELECT staff_id FROM staff_accounts WHERE username = 'lab1'").value_or(0) == 4);
    }
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}
