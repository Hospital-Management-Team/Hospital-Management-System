// TC-Rec-01 for issue #6 (SRS HMS-F-003, HMS-SR-001, HMS-SR-004): versioned
// medical history, care-relationship access and its audit entries. Integration
// tests against a real SQLite store.
#include <doctest.h>

#include <string>

#include "medical_record_test_support.hpp"

using namespace hms;
using test::at;
using test::doctorSession;
using test::kMonday;
using test::MedicalRecordFixture;

TEST_CASE("TC-Rec-01 a new patient has no history yet, and opening it is audited") {
    MedicalRecordFixture f;
    const auto doc = f.addDoctor("mehta");
    const auto uhid = f.addPatient("Ravi Kumar");
    f.bookOk(uhid, doc, at(kMonday, 9, 0));

    auto r = f.records.openHistory(doctorSession("mehta"), uhid);
    REQUIRE(r.ok());
    CHECK(r.value().patientName == "Ravi Kumar");
    CHECK(r.value().versions.empty());
    CHECK(r.value().current() == nullptr);
    CHECK(f.count("SELECT count(*) FROM audit_entry WHERE action = 'MEDICAL_HISTORY_VIEW' AND outcome = 'OK' "
                  "AND record_id = 'HMS-000001' AND user_id = 'mehta'") == 1);
}

TEST_CASE("TC-Rec-01 each update is a new version with its author and time; earlier versions are kept") {
    MedicalRecordFixture f;
    const auto doc = f.addDoctor("mehta");
    const auto uhid = f.addPatient("Ravi Kumar");
    f.bookOk(uhid, doc, at(kMonday, 9, 0));

    const auto first = f.updateOk("mehta", uhid, f.history("Type 2 diabetes since 2019", "Penicillin", "Metformin 500"));
    CHECK(formatHistoryVersionId(first.versionId) == "MHV-000001");
    CHECK(first.authorUsername == "mehta");
    CHECK(first.authorStaffId == doc);
    CHECK(first.recordedAt == "2026-10-25T06:00:00Z");

    f.clock.advance(std::chrono::minutes(10));
    const auto second = f.updateOk("mehta", uhid, f.history("Type 2 diabetes since 2019", "Penicillin, sulfa drugs",
                                                           "Metformin 500"));
    CHECK(second.versionId > first.versionId);
    CHECK(second.recordedAt == "2026-10-25T06:10:00Z");

    auto h = f.records.openHistory(doctorSession("mehta"), uhid);
    REQUIRE(h.ok());
    REQUIRE(h.value().versions.size() == 2);
    CHECK(h.value().current()->versionId == second.versionId);  // newest first
    CHECK(h.value().current()->allergies == "Penicillin, sulfa drugs");
    CHECK(h.value().versions[1].allergies == "Penicillin");     // the earlier version is unchanged

    // Only the field that changed is named in the audit entry.
    CHECK(f.count("SELECT count(*) FROM audit_entry WHERE action = 'MEDICAL_HISTORY_UPDATE' AND outcome = 'OK' "
                  "AND record_id = 'HMS-000001'") == 2);
    CHECK(f.count("SELECT count(*) FROM audit_entry WHERE action = 'MEDICAL_HISTORY_UPDATE' "
                  "AND changed_fields = 'history,allergies,current_medications'") == 1);
    CHECK(f.count("SELECT count(*) FROM audit_entry WHERE action = 'MEDICAL_HISTORY_UPDATE' "
                  "AND changed_fields = 'allergies'") == 1);
}

TEST_CASE("TC-Rec-01 audit entries hold the UHID and field names only, never the clinical text") {
    MedicalRecordFixture f;
    const auto doc = f.addDoctor("mehta");
    const auto uhid = f.addPatient("Ravi Kumar");
    f.bookOk(uhid, doc, at(kMonday, 9, 0));
    f.updateOk("mehta", uhid, f.history("Asthma", "Peanuts", "Salbutamol inhaler"));
    REQUIRE(f.records.openHistory(doctorSession("mehta"), uhid).ok());

    for (const char* secret : {"Asthma", "Peanuts", "Salbutamol", "Ravi", "Kumar"}) {
        CHECK(f.count(std::string("SELECT count(*) FROM audit_entry WHERE ifnull(record_id,'') || ifnull(changed_fields,'') "
                                  "|| ifnull(reason_code,'') LIKE '%") + secret + "%'") == 0);
    }
}

TEST_CASE("TC-Rec-01 a doctor without a care relationship is denied, nothing is saved, and the denial is audited") {
    MedicalRecordFixture f;
    const auto doc = f.addDoctor("mehta");
    f.addDoctor("rao");
    const auto uhid = f.addPatient("Ravi Kumar");
    f.bookOk(uhid, doc, at(kMonday, 9, 0));
    f.updateOk("mehta", uhid, f.history("Asthma"));

    auto open = f.records.openHistory(doctorSession("rao"), uhid);
    REQUIRE_FALSE(open.ok());
    CHECK(open.error().kind == ErrorKind::AccessDenied);
    auto update = f.records.updateHistory(doctorSession("rao"), uhid, f.history("Something else"));
    REQUIRE_FALSE(update.ok());
    CHECK(update.error().kind == ErrorKind::AccessDenied);

    CHECK(f.count("SELECT count(*) FROM medical_history_version") == 1);
    CHECK(f.count("SELECT count(*) FROM audit_entry WHERE user_id = 'rao' AND outcome = 'DENIED' "
                  "AND reason_code = 'NO_CARE_RELATIONSHIP' AND record_id = 'HMS-000001' "
                  "AND action IN ('MEDICAL_HISTORY_VIEW', 'MEDICAL_HISTORY_UPDATE')") == 2);

    // A Doctor account with no profile has no appointments, so no care relationship either.
    f.addStaff("newdoc");
    CHECK(f.records.openHistory(doctorSession("newdoc"), uhid).error().kind == ErrorKind::AccessDenied);
}

TEST_CASE("TC-Rec-01 cancelling the doctor's only appointment ends access, also to the versions that doctor wrote") {
    MedicalRecordFixture f;
    const auto doc = f.addDoctor("mehta");
    const auto uhid = f.addPatient("Ravi Kumar");
    const auto appt = f.bookOk(uhid, doc, at(kMonday, 9, 0));
    f.updateOk("mehta", uhid, f.history("Asthma"));

    REQUIRE(f.appointments->cancel(test::receptionist(), appt.id).ok());
    auto r = f.records.openHistory(doctorSession("mehta"), uhid);
    REQUIRE_FALSE(r.ok());
    CHECK(r.error().kind == ErrorKind::AccessDenied);
    CHECK(f.count("SELECT count(*) FROM audit_entry WHERE action = 'MEDICAL_HISTORY_VIEW' AND outcome = 'DENIED' "
                  "AND user_id = 'mehta' AND reason_code = 'NO_CARE_RELATIONSHIP'") == 1);

    // A rescheduled appointment does not count either; its replacement does.
    const auto again = f.bookOk(uhid, doc, at(kMonday, 10, 0));
    REQUIRE(f.appointments->reschedule(test::receptionist(), again.id, at(kMonday, 11, 0)).ok());
    CHECK(f.records.openHistory(doctorSession("mehta"), uhid).ok());
}

TEST_CASE("TC-Rec-01 a completed visit keeps the care relationship") {
    MedicalRecordFixture f;
    const auto doc = f.addDoctor("mehta");
    const auto uhid = f.addPatient("Ravi Kumar");
    const auto appt = f.visit(uhid, doc);
    REQUIRE(f.consultations.recordConsultation(doctorSession("mehta"), f.draft(appt.id)).ok());
    REQUIRE(f.count("SELECT count(*) FROM appointment WHERE status = 'COMPLETED'") == 1);

    CHECK(f.records.openHistory(doctorSession("mehta"), uhid).ok());
    f.updateOk("mehta", uhid, f.history("Viral fever, recovered"));
}

TEST_CASE("TC-Rec-01 Receptionist, Pharmacist and Admin are denied the history, and each denial is audited") {
    MedicalRecordFixture f;
    const auto doc = f.addDoctor("mehta");
    const auto uhid = f.addPatient("Ravi Kumar");
    f.bookOk(uhid, doc, at(kMonday, 9, 0));
    f.updateOk("mehta", uhid, f.history("Asthma"));

    for (const Session& s : {test::receptionist(), test::admin(), test::pharmacist()}) {
        auto open = f.records.openHistory(s, uhid);
        REQUIRE_FALSE(open.ok());
        CHECK(open.error().kind == ErrorKind::AccessDenied);
        auto update = f.records.updateHistory(s, uhid, f.history("x"));
        REQUIRE_FALSE(update.ok());
        CHECK(update.error().kind == ErrorKind::AccessDenied);
        CHECK(f.count("SELECT count(*) FROM audit_entry WHERE outcome = 'DENIED' AND user_id = '" + s.userId +
                      "' AND action IN ('MEDICAL_HISTORY_VIEW', 'MEDICAL_HISTORY_UPDATE')") == 2);
    }
    CHECK(f.count("SELECT count(*) FROM medical_history_version") == 1);
}

TEST_CASE("TC-Rec-01 prescribing does not change the current-medications list") {
    MedicalRecordFixture f;
    const auto doc = f.addDoctor("mehta");
    const auto uhid = f.addPatient("Ravi Kumar");
    const auto appt = f.visit(uhid, doc);
    f.updateOk("mehta", uhid, f.history("Type 2 diabetes", "", "Metformin 500"));

    auto d = f.draft(appt.id);
    d.prescriptions.push_back({"Paracetamol 500", "1 tablet twice a day for 5 days", "10"});
    REQUIRE(f.consultations.recordConsultation(doctorSession("mehta"), d).ok());

    auto h = f.records.openHistory(doctorSession("mehta"), uhid);
    REQUIRE(h.ok());
    REQUIRE(h.value().versions.size() == 1);
    CHECK(h.value().current()->currentMedications == "Metformin 500");
}

TEST_CASE("TC-Rec-01 invalid, empty or unchanged updates are refused and nothing is saved") {
    MedicalRecordFixture f;
    const auto doc = f.addDoctor("mehta");
    const auto uhid = f.addPatient("Ravi Kumar");
    f.bookOk(uhid, doc, at(kMonday, 9, 0));
    const Session mehta = doctorSession("mehta");

    auto tooLong = f.records.updateHistory(mehta, uhid, f.history(std::string(1001, 'x')));
    REQUIRE_FALSE(tooLong.ok());
    CHECK(tooLong.error().kind == ErrorKind::Validation);
    CHECK(f.records.updateHistory(mehta, uhid, f.history(std::string(1000, 'x'))).ok());  // the limit itself is fine

    f.updateOk("mehta", uhid, f.history("Asthma"));
    auto unchanged = f.records.updateHistory(mehta, uhid, f.history("  Asthma "));  // the same once trimmed
    REQUIRE_FALSE(unchanged.ok());
    CHECK(unchanged.error().kind == ErrorKind::Validation);

    const auto other = f.addPatient("Asha Rao");
    f.bookOk(other, doc, at(kMonday, 10, 0));
    auto empty = f.records.updateHistory(mehta, other, f.history("", " ", ""));
    REQUIRE_FALSE(empty.ok());
    CHECK(empty.error().kind == ErrorKind::Validation);

    auto missing = f.records.openHistory(mehta, 999);
    REQUIRE_FALSE(missing.ok());
    CHECK(missing.error().kind == ErrorKind::NotFound);

    CHECK(f.count("SELECT count(*) FROM medical_history_version") == 2);
    CHECK(f.count("SELECT count(*) FROM audit_entry WHERE action = 'MEDICAL_HISTORY_UPDATE'") == 2);
}

TEST_CASE("TC-Rec-01 a failure before commit saves neither the version nor its audit entry") {
    MedicalRecordFixture f;
    const auto doc = f.addDoctor("mehta");
    const auto uhid = f.addPatient("Ravi Kumar");
    f.bookOk(uhid, doc, at(kMonday, 9, 0));

    f.db().setFaultHook([](const char* point) {
        if (std::string(point) == "medical_history_before_commit") throw InjectedFault(point);
    });
    auto failed = f.records.updateHistory(doctorSession("mehta"), uhid, f.history("Asthma"));
    f.db().setFaultHook(nullptr);
    REQUIRE_FALSE(failed.ok());
    CHECK(failed.error().kind == ErrorKind::StorageFailure);
    CHECK(f.count("SELECT count(*) FROM medical_history_version") == 0);
    CHECK(f.count("SELECT count(*) FROM audit_entry WHERE action = 'MEDICAL_HISTORY_UPDATE'") == 0);
    f.updateOk("mehta", uhid, f.history("Asthma"));
}

TEST_CASE("TC-Rec-01 the store refuses to change or delete a saved version") {
    MedicalRecordFixture f;
    const auto doc = f.addDoctor("mehta");
    const auto uhid = f.addPatient("Ravi Kumar");
    f.bookOk(uhid, doc, at(kMonday, 9, 0));
    f.updateOk("mehta", uhid, f.history("Asthma"));

    CHECK_THROWS_AS(f.db().exec("UPDATE medical_history_version SET history = 'changed'"), DbError);
    CHECK_THROWS_AS(f.db().exec("DELETE FROM medical_history_version"), DbError);
    CHECK(f.count("SELECT count(*) FROM medical_history_version WHERE history = 'Asthma'") == 1);
}
