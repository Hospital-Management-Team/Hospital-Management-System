// TC-Doc-01 (own hours), TC-Doc-02 (weekly queue) and TC-Doc-03 (consultation)
// for issue #5 (SRS HMS-F-020..022). Integration tests against a real SQLite store.
#include <doctest.h>

#include <string>

#include "consultation_test_support.hpp"

using namespace hms;
using test::at;
using test::ConsultationFixture;
using test::doctorSession;
using test::kMonday;
using test::kTuesday;

TEST_CASE("TC-Doc-03 a consultation is saved with its prescription and tests, linked to the patient and the appointment") {
    ConsultationFixture f;
    const auto doc = f.addDoctor("mehta");
    const auto uhid = f.addPatient("Ravi Kumar");
    const auto appt = f.visit(uhid, doc);

    auto d = f.draft(appt.id);
    d.prescriptions.push_back({"Paracetamol 500", "1 tablet twice a day for 5 days", "10"});
    d.labOrders.push_back({"Complete Blood Count", "Fasting"});
    d.labOrders.push_back({"Chest X-ray", ""});
    auto r = f.consultations.recordConsultation(doctorSession("mehta"), d);
    REQUIRE(r.ok());
    CHECK(r.value().uhid == uhid);
    CHECK(r.value().appointmentId == appt.id);
    CHECK(r.value().doctorUsername == "mehta");
    CHECK(r.value().diagnosis == "Viral fever");
    REQUIRE(r.value().prescriptions.size() == 1);
    CHECK(r.value().prescriptions[0].quantity == 10);
    REQUIRE(r.value().labOrders.size() == 2);
    CHECK(r.value().labOrders[0].instructions.value() == "Fasting");
    CHECK_FALSE(r.value().labOrders[1].instructions.has_value());
    CHECK(formatConsultationId(r.value().consultationId) == "CON-000001");

    CHECK(f.count("SELECT count(*) FROM appointment WHERE status = 'COMPLETED'") == 1);
    CHECK(f.count("SELECT count(*) FROM consultation WHERE uhid = " + std::to_string(uhid)) == 1);

    // The visit is done: the same appointment cannot be recorded twice.
    auto again = f.consultations.recordConsultation(doctorSession("mehta"), d);
    REQUIRE_FALSE(again.ok());
    CHECK(again.error().kind == ErrorKind::Conflict);
    CHECK(f.count("SELECT count(*) FROM consultation") == 1);
}

TEST_CASE("TC-Doc-03 the audit entry names the consultation and fields only, never the clinical text") {
    ConsultationFixture f;
    const auto doc = f.addDoctor("mehta");
    const auto uhid = f.addPatient("Ravi Kumar");
    const auto appt = f.visit(uhid, doc);
    auto d = f.draft(appt.id, "Hypertension");
    d.prescriptions.push_back({"Amlodipine 5", "1 tablet daily", "30"});
    d.labOrders.push_back({"Lipid Profile", ""});
    REQUIRE(f.consultations.recordConsultation(doctorSession("mehta"), d).ok());

    CHECK(f.count("SELECT count(*) FROM audit_entry WHERE action = 'CONSULTATION_RECORD' AND outcome = 'OK' "
                  "AND record_id = 'CON-000001' AND user_id = 'mehta'") == 1);
    for (const char* secret : {"Hypertension", "Amlodipine", "Lipid", "Ravi", "Kumar"}) {
        CHECK(f.count(std::string("SELECT count(*) FROM audit_entry WHERE ifnull(record_id,'') || ifnull(changed_fields,'') "
                                  "|| ifnull(reason_code,'') LIKE '%") + secret + "%'") == 0);
    }
}

TEST_CASE("TC-Doc-03 invalid or incomplete records are rejected and nothing is saved") {
    ConsultationFixture f;
    const auto doc = f.addDoctor("mehta");
    const auto appt = f.visit(f.addPatient("Ravi Kumar"), doc);
    const auto doctor = doctorSession("mehta");

    auto expectInvalid = [&](const ConsultationDraft& d, const std::string& field) {
        auto r = f.consultations.recordConsultation(doctor, d);
        REQUIRE_FALSE(r.ok());
        CHECK(r.error().kind == ErrorKind::Validation);
        bool named = false;
        for (const auto& fe : r.error().fields) named = named || fe.field.find(field) != std::string::npos;
        CHECK_MESSAGE(named, "no error names " << field);
    };

    expectInvalid(f.draft(appt.id, ""), "Diagnosis");
    expectInvalid(f.draft(appt.id, "   "), "Diagnosis");
    expectInvalid(f.draft(appt.id, std::string(1001, 'x')), "Diagnosis");
    expectInvalid(f.draft(0), "Appointment");

    auto d = f.draft(appt.id);
    d.prescriptions = {{"", "1 daily", "5"}};
    expectInvalid(d, "medicine");
    d.prescriptions = {{"Paracetamol", "", "5"}};
    expectInvalid(d, "dosage");
    for (const char* bad : {"0", "-3", "abc", "", "1001", "2.5"}) {
        d.prescriptions = {{"Paracetamol", "1 daily", bad}};
        expectInvalid(d, "quantity");
    }
    d.prescriptions.clear();
    d.labOrders = {{"", ""}};
    expectInvalid(d, "test");
    d.labOrders = {{"CBC", std::string(201, 'x')}};
    expectInvalid(d, "instructions");
    d.labOrders.clear();
    d.prescriptions.assign(21, PrescriptionDraft{"Paracetamol", "1 daily", "5"});
    expectInvalid(d, "Prescription");

    // Several problems are reported together.
    d = f.draft(appt.id, "");
    d.prescriptions = {{"", "", "0"}};
    auto many = f.consultations.recordConsultation(doctor, d);
    REQUIRE_FALSE(many.ok());
    CHECK(many.error().fields.size() >= 4);

    auto unknown = f.consultations.recordConsultation(doctor, f.draft(9999));
    REQUIRE_FALSE(unknown.ok());
    CHECK(unknown.error().kind == ErrorKind::NotFound);

    CHECK(f.count("SELECT count(*) FROM consultation") == 0);
    CHECK(f.count("SELECT count(*) FROM prescription_item") == 0);
    CHECK(f.count("SELECT count(*) FROM lab_order") == 0);
    CHECK(f.count("SELECT count(*) FROM appointment WHERE status = 'BOOKED'") == 1);
}

TEST_CASE("TC-Doc-03 only the appointment's own doctor can record it, and only while it is booked") {
    ConsultationFixture f;
    const auto mehta = f.addDoctor("mehta");
    f.addDoctor("rao");
    const auto uhid = f.addPatient("Ravi Kumar");
    const auto appt = f.visit(uhid, mehta);

    auto other = f.consultations.recordConsultation(doctorSession("rao"), f.draft(appt.id));
    REQUIRE_FALSE(other.ok());
    CHECK(other.error().kind == ErrorKind::AccessDenied);
    CHECK(f.count("SELECT count(*) FROM audit_entry WHERE action = 'CONSULTATION_RECORD' AND outcome = 'DENIED' "
                  "AND user_id = 'rao'") == 1);

    for (const Session& s : {test::receptionist(), test::admin(), Session{"pat", "pat", Role::Pharmacist, false}}) {
        auto r = f.consultations.recordConsultation(s, f.draft(appt.id));
        REQUIRE_FALSE(r.ok());
        CHECK(r.error().kind == ErrorKind::AccessDenied);
    }

    // A Doctor account with no profile cannot record.
    f.addStaff("newdoc");
    auto noProfile = f.consultations.recordConsultation(doctorSession("newdoc"), f.draft(appt.id));
    REQUIRE_FALSE(noProfile.ok());

    f.db().exec("UPDATE appointment SET status = 'CANCELLED', cancelled_at = '2026-10-26T03:00:00Z', "
                "cancelled_by = 'x' WHERE appointment_id = " + std::to_string(appt.id));
    auto cancelled = f.consultations.recordConsultation(doctorSession("mehta"), f.draft(appt.id));
    REQUIRE_FALSE(cancelled.ok());
    CHECK(cancelled.error().kind == ErrorKind::Conflict);
    CHECK(f.count("SELECT count(*) FROM consultation") == 0);
}

TEST_CASE("TC-Doc-03 the store itself refuses a consultation that does not match its appointment") {
    ConsultationFixture f;
    const auto mehta = f.addDoctor("mehta");
    const auto rao = f.addDoctor("rao");
    const auto ravi = f.addPatient("Ravi Kumar");
    const auto anita = f.addPatient("Anita Rao");
    const auto appt = f.visit(ravi, mehta);
    auto insert = [&](std::int64_t uhid, std::int64_t doctor) {
        f.db().exec("INSERT INTO consultation (appointment_id, uhid, doctor_staff_id, diagnosis, recorded_by, recorded_at) "
                    "VALUES (" + std::to_string(appt.id) + ", " + std::to_string(uhid) + ", " + std::to_string(doctor) +
                    ", 'x', 'x', '2026-10-26T04:00:00Z')");
    };
    CHECK_THROWS(insert(anita, mehta));  // wrong patient
    CHECK_THROWS(insert(ravi, rao));     // wrong doctor
    CHECK_NOTHROW(insert(ravi, mehta));
}

TEST_CASE("TC-Doc-03 a failed save leaves nothing half-written") {
    ConsultationFixture f;
    const auto doc = f.addDoctor("mehta");
    const auto appt = f.visit(f.addPatient("Ravi Kumar"), doc);
    auto d = f.draft(appt.id);
    d.prescriptions.push_back({"Paracetamol", "1 daily", "5"});
    d.labOrders.push_back({"CBC", ""});

    f.db().setFaultHook([](const char* point) {
        if (std::string(point) == "consultation_before_commit") throw InjectedFault(point);
    });
    auto failed = f.consultations.recordConsultation(doctorSession("mehta"), d);
    f.db().setFaultHook(nullptr);
    REQUIRE_FALSE(failed.ok());
    CHECK(failed.error().kind == ErrorKind::StorageFailure);
    CHECK(f.count("SELECT count(*) FROM consultation") == 0);
    CHECK(f.count("SELECT count(*) FROM prescription_item") == 0);
    CHECK(f.count("SELECT count(*) FROM lab_order") == 0);
    CHECK(f.count("SELECT count(*) FROM appointment WHERE status = 'BOOKED'") == 1);
    CHECK(f.count("SELECT count(*) FROM audit_entry WHERE action = 'CONSULTATION_RECORD' AND outcome = 'OK'") == 0);

    CHECK(f.consultations.recordConsultation(doctorSession("mehta"), d).ok());  // the retry works
}

TEST_CASE("TC-Doc-03 a doctor sees a patient's earlier consultations, newest first; others cannot") {
    ConsultationFixture f;
    const auto doc = f.addDoctor("mehta");
    const auto uhid = f.addPatient("Ravi Kumar");
    const auto other = f.addPatient("Anita Rao");
    const auto first = f.visit(uhid, doc, at(kMonday, 9, 0));
    REQUIRE(f.consultations.recordConsultation(doctorSession("mehta"), f.draft(first.id, "Cough")).ok());
    const auto second = f.visit(uhid, doc, at(kTuesday, 9, 0));
    REQUIRE(f.consultations.recordConsultation(doctorSession("mehta"), f.draft(second.id, "Fever")).ok());
    const auto third = f.bookOk(other, doc, at(kTuesday, 12, 0));
    REQUIRE(f.consultations.recordConsultation(doctorSession("mehta"), f.draft(third.id, "Rash")).ok());

    auto h = f.consultations.listPatientHistory(doctorSession("mehta"), uhid);
    REQUIRE(h.ok());
    REQUIRE(h.value().size() == 2);
    CHECK(h.value()[0].diagnosis == "Fever");
    CHECK(h.value()[1].diagnosis == "Cough");
    CHECK(h.value()[0].patientName == "Ravi Kumar");  // no other patient's record is included
    CHECK(f.count("SELECT count(*) FROM audit_entry WHERE action = 'CONSULTATION_VIEW' AND outcome = 'OK'") == 1);

    CHECK(f.consultations.listPatientHistory(test::receptionist(), uhid).error().kind == ErrorKind::AccessDenied);
    CHECK(f.consultations.listPatientHistory(test::admin(), uhid).error().kind == ErrorKind::AccessDenied);
    CHECK(f.consultations.listPatientHistory(doctorSession("mehta"), 999).error().kind == ErrorKind::NotFound);
}

TEST_CASE("TC-Doc-02 the weekly queue shows the doctor's own appointments for seven days, in time order") {
    ConsultationFixture f;
    const auto mehta = f.addDoctor("mehta");
    const auto rao = f.addDoctor("rao");
    const auto ravi = f.addPatient("Ravi Kumar");
    const auto anita = f.addPatient("Anita Rao");
    const auto tue = f.bookOk(ravi, mehta, at(kTuesday, 10, 0));
    const auto mon2 = f.bookOk(anita, mehta, at(kMonday, 9, 15));
    f.bookOk(ravi, mehta, at(kMonday, 9, 0));
    f.bookOk(anita, rao, at(kMonday, 9, 0));                      // another doctor's patient
    f.bookOk(anita, mehta, at(Date{2026, 11, 1}, 9, 0));            // last day of the week
    f.bookOk(ravi, mehta, at(Date{2026, 11, 2}, 9, 0));             // day eight: not shown
    const auto cancelled = f.bookOk(ravi, mehta, at(kMonday, 9, 30));
    REQUIRE(f.appointments->cancel(test::receptionist(), cancelled.id).ok());

    auto week = f.appointments->myWeekQueue(doctorSession("mehta"), kMonday);
    REQUIRE(week.ok());
    REQUIRE(week.value().size() == 4);
    CHECK(week.value()[0].start == at(kMonday, 9, 0));
    CHECK(week.value()[1].id == mon2.id);
    CHECK(week.value()[2].id == tue.id);
    CHECK(week.value()[3].start.date == Date{2026, 11, 1});
    for (const auto& a : week.value()) CHECK(a.doctorUsername == "mehta");

    auto raoWeek = f.appointments->myWeekQueue(doctorSession("rao"), kMonday);
    REQUIRE(raoWeek.ok());
    CHECK(raoWeek.value().size() == 1);

    auto denied = f.appointments->myWeekQueue(test::receptionist(), kMonday);
    REQUIRE_FALSE(denied.ok());
    CHECK(denied.error().kind == ErrorKind::AccessDenied);
}

TEST_CASE("TC-Doc-01 a doctor changes only their own weekly hours; the Admin's department stays") {
    ConsultationFixture f;
    const auto mehta = f.addDoctor("mehta", "Cardiology");
    const auto rao = f.addDoctor("rao", "Neurology");
    const auto doctor = doctorSession("mehta");

    auto before = f.appointments->myProfile(doctor);
    REQUIRE(before.ok());
    CHECK(before.value().hours.size() == 7);

    // Emergency: off on Tuesday, shorter on Monday.
    auto saved = f.appointments->setOwnHours(doctor, {WeeklyHours{1, 10 * 60, 12 * 60}, WeeklyHours{3, 9 * 60, 13 * 60}});
    REQUIRE(saved.ok());
    auto after = f.appointments->myProfile(doctor);
    REQUIRE(after.ok());
    REQUIRE(after.value().hours.size() == 2);
    CHECK(after.value().hoursOn(1)->startMinute == 600);
    CHECK_FALSE(after.value().hoursOn(2).has_value());
    CHECK(after.value().department == "Cardiology");

    // The other doctor's hours did not change.
    auto raoProfile = f.appointments->listDoctors(test::admin());
    REQUIRE(raoProfile.ok());
    for (const auto& d : raoProfile.value()) {
        if (d.staffId == rao) CHECK(d.hours.size() == 7);
        if (d.staffId == mehta) CHECK(d.hours.size() == 2);
    }

    // Booking follows the new hours at once: no free Tuesday slot.
    auto tuesday = f.appointments->freeSlots(test::receptionist(), mehta, kTuesday);
    REQUIRE(tuesday.ok());
    CHECK(tuesday.value().empty());

    CHECK(f.count("SELECT count(*) FROM audit_entry WHERE action = 'DOCTOR_SCHEDULE_SET' AND outcome = 'OK' "
                  "AND user_id = 'mehta'") == 1);
}

TEST_CASE("TC-Doc-01 own hours: invalid hours, other roles and doctors without a profile are refused") {
    ConsultationFixture f;
    f.addDoctor("mehta");
    const auto doctor = doctorSession("mehta");

    auto backwards = f.appointments->setOwnHours(doctor, {WeeklyHours{1, 13 * 60, 9 * 60}});
    REQUIRE_FALSE(backwards.ok());
    CHECK(backwards.error().kind == ErrorKind::Validation);
    auto twice = f.appointments->setOwnHours(doctor, {WeeklyHours{1, 9 * 60, 12 * 60}, WeeklyHours{1, 13 * 60, 17 * 60}});
    CHECK_FALSE(twice.ok());
    auto badDay = f.appointments->setOwnHours(doctor, {WeeklyHours{8, 9 * 60, 12 * 60}});
    CHECK_FALSE(badDay.ok());
    CHECK(f.appointments->myProfile(doctor).value().hours.size() == 7);  // unchanged

    for (const Session& s : {test::receptionist(), test::admin()}) {
        CHECK(f.appointments->setOwnHours(s, {WeeklyHours{1, 9 * 60, 12 * 60}}).error().kind == ErrorKind::AccessDenied);
        CHECK(f.appointments->myProfile(s).error().kind == ErrorKind::AccessDenied);
    }
    CHECK(f.count("SELECT count(*) FROM audit_entry WHERE action = 'DOCTOR_SCHEDULE_SET' AND outcome = 'DENIED'") >= 2);

    f.addStaff("newdoc");  // a Doctor login the Admin has not given a department yet
    auto noProfile = f.appointments->setOwnHours(doctorSession("newdoc"), {WeeklyHours{1, 9 * 60, 12 * 60}});
    REQUIRE_FALSE(noProfile.ok());
    CHECK(noProfile.error().kind == ErrorKind::Conflict);
    CHECK(f.appointments->setOwnHours(doctorSession("ghost"), {WeeklyHours{1, 9 * 60, 12 * 60}}).error().kind ==
          ErrorKind::NotFound);
}

TEST_CASE("TC-Doc-01 own hours: booked appointments that no longer fit are kept and counted") {
    ConsultationFixture f;
    const auto doc = f.addDoctor("mehta");
    f.bookOk(f.addPatient("Ravi Kumar"), doc, at(kMonday, 9, 0));
    f.bookOk(f.addPatient("Anita Rao"), doc, at(kMonday, 11, 0));
    auto saved = f.appointments->setOwnHours(doctorSession("mehta"), {WeeklyHours{1, 10 * 60, 13 * 60}});
    REQUIRE(saved.ok());
    CHECK(saved.value().bookedOutsideHours == 1);
    CHECK(f.count("SELECT count(*) FROM appointment WHERE status = 'BOOKED'") == 2);
}

TEST_CASE("TC-App-02 a completed visit keeps its slot until the slot ends") {
    ConsultationFixture f;
    const auto doc = f.addDoctor("mehta");
    const auto first = f.addPatient("Ravi Kumar");
    const auto second = f.addPatient("Anita Rao");
    const auto appt = f.bookOk(first, doc, at(kMonday, 9, 0));  // 09:00-09:15

    // The doctor records the visit at 09:05, while its slot is still running.
    f.setLocalTime(kMonday, 9, 5);
    REQUIRE(f.consultations.recordConsultation(doctorSession("mehta"), f.draft(appt.id)).ok());

    // Monday's hours now start at 09:10, so the new grid has a 09:10 slot that
    // overlaps the completed 09:00-09:15 visit.
    DoctorScheduleDraft d;
    d.department = "General Medicine";
    for (int day = 1; day <= 7; ++day) d.hours.push_back(WeeklyHours{day, day == 1 ? 9 * 60 + 10 : 9 * 60, 13 * 60});
    REQUIRE(f.appointments->setDoctorSchedule(test::admin(), doc, d).ok());

    auto slots = f.appointments->freeSlots(test::receptionist(), doc, kMonday);
    REQUIRE(slots.ok());
    REQUIRE_FALSE(slots.value().empty());
    CHECK(slots.value().front() == at(kMonday, 9, 25));

    auto overlapping = f.appointments->book(test::receptionist(), second, doc, at(kMonday, 9, 10));
    REQUIRE_FALSE(overlapping.ok());
    CHECK(overlapping.error().kind == ErrorKind::Conflict);
    CHECK(f.appointments->book(test::receptionist(), second, doc, at(kMonday, 9, 25)).ok());
}
