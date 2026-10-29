// Issue #4: doctor schedules, booking, double-booking prevention, reschedule,
// cancel, reminders and the doctor's queue (SRS HMS-F-010..F-013, F-020, F-021).
// Test names start with the STP test case ID; the number in brackets is the
// criterion in docs/deviation-log.md.
#include <doctest.h>

#include "appointment_test_support.hpp"
#include "validation/input.hpp"

using namespace hms;
using test::AppointmentFixture;
using test::at;
using test::kMonday;
using test::kToday;
using test::kTuesday;

namespace {
bool hasSlot(const std::vector<LocalSlot>& slots, const LocalSlot& s) {
    for (const auto& x : slots)
        if (x == s) return true;
    return false;
}
}  // namespace

// ------------------------------------------------------------ helpers

TEST_CASE("Appointment helpers: slot keys, weekdays, appointment numbers, HH:MM input") {
    CHECK(isoWeekday(kToday) == 7);   // 25-10-2026 is a Sunday
    CHECK(isoWeekday(kMonday) == 1);
    CHECK(isoWeekday(Date{1970, 1, 1}) == 4);

    const LocalSlot s = at(kMonday, 9, 5);
    CHECK(s.toKey() == "2026-10-26T09:05");
    CHECK(s.toDisplay() == "26-10-2026 09:05");
    REQUIRE(LocalSlot::fromKey("2026-10-26T09:05").has_value());
    CHECK(*LocalSlot::fromKey("2026-10-26T09:05") == s);
    CHECK_FALSE(LocalSlot::fromKey("2026-10-26 09:05").has_value());
    CHECK_FALSE(LocalSlot::fromKey("2026-10-26T24:00").has_value());

    CHECK(formatAppointmentId(12) == "APT-000012");
    CHECK(parseAppointmentId("APT-000012") == 12);
    CHECK(parseAppointmentId("apt12") == 12);
    CHECK(parseAppointmentId(" 12 ") == 12);
    CHECK_FALSE(parseAppointmentId("APT-").has_value());
    CHECK_FALSE(parseAppointmentId("0").has_value());

    CHECK(input::parseTimeOfDay("09:30", "T").value() == 570);
    CHECK(input::parseTimeOfDay("9:30", "T").value() == 570);
    CHECK(input::parseTimeOfDay("23:59", "T").value() == 1439);
    CHECK_FALSE(input::parseTimeOfDay("24:00", "T").ok());
    CHECK_FALSE(input::parseTimeOfDay("9.30", "T").ok());
    CHECK_FALSE(input::parseTimeOfDay("09:3", "T").ok());
    CHECK_FALSE(input::parseTimeOfDay("", "T").ok());

    // The local time of the fixed test clock.
    FixedClock c{test::fixedNow()};
    CHECK(localSlotAt(c.now(), 330) == at(kToday, 11, 30));
}

TEST_CASE("Migration 4 adds the appointment tables to a new store") {
    AppointmentFixture f;
    for (const char* t : {"doctor_profile", "doctor_availability", "appointment", "appointment_reminder"}) {
        CHECK(f.count(std::string("SELECT count(*) FROM sqlite_master WHERE type='table' AND name='") + t + "'") == 1);
    }
}

// ------------------------------------------------------------ TC-Doc-01 (HMS-F-020)

TEST_CASE("TC-Doc-01 (1) a doctor appears for booking only on the weekdays of their hours") {
    AppointmentFixture f;
    const auto mondayOnly = f.addStaff("mehta");
    DoctorScheduleDraft d;
    d.department = "Cardiology";
    d.specialization = "Interventional cardiology";
    d.hours = {WeeklyHours{1, 9 * 60, 12 * 60}};
    auto saved = f.appointments->setDoctorSchedule(test::admin(), mondayOnly, d);
    REQUIRE(saved.ok());
    f.addStaff("no_hours_yet");  // a Doctor account with no profile

    auto monday = f.appointments->doctorsOn(test::receptionist(), kMonday);
    REQUIRE(monday.ok());
    REQUIRE(monday.value().size() == 1);
    CHECK(monday.value()[0].doctor.username == "mehta");
    CHECK(monday.value()[0].doctor.department == "Cardiology");
    CHECK(monday.value()[0].freeSlots.size() == 12);  // 09:00-12:00 in 15-minute slots
    CHECK(monday.value()[0].freeSlots.front() == at(kMonday, 9, 0));
    CHECK(monday.value()[0].freeSlots.back() == at(kMonday, 11, 45));

    auto tuesday = f.appointments->doctorsOn(test::receptionist(), kTuesday);
    REQUIRE(tuesday.ok());
    CHECK(tuesday.value().empty());

    CHECK(f.count("SELECT count(*) FROM audit_entry WHERE action = 'DOCTOR_SCHEDULE_SET' AND outcome = 'OK' "
                  "AND record_id = 'STAFF-" + std::to_string(mondayOnly) + "'") == 1);
}

TEST_CASE("TC-Doc-01 (2) schedule input is validated and only the Admin may change it") {
    AppointmentFixture f;
    const auto doc = f.addStaff("mehta");

    DoctorScheduleDraft bad;
    bad.department = "  ";
    bad.hours = {WeeklyHours{1, 13 * 60, 9 * 60}, WeeklyHours{2, 9 * 60, 9 * 60 + 10}, WeeklyHours{2, 9 * 60, 10 * 60}};
    auto r = f.appointments->setDoctorSchedule(test::admin(), doc, bad);
    REQUIRE_FALSE(r.ok());
    CHECK(r.error().kind == ErrorKind::Validation);
    const std::string all = r.error().describe();
    CHECK(all.find("Department") != std::string::npos);
    CHECK(all.find("end time must be after the start time") != std::string::npos);
    CHECK(all.find("at least 15 minutes") != std::string::npos);
    CHECK(all.find("only once") != std::string::npos);
    CHECK(f.count("SELECT count(*) FROM doctor_profile") == 0);

    DoctorScheduleDraft ok;
    ok.department = "ENT";
    ok.hours = {WeeklyHours{1, 9 * 60, 10 * 60}};
    auto denied = f.appointments->setDoctorSchedule(test::receptionist(), doc, ok);
    REQUIRE_FALSE(denied.ok());
    CHECK(denied.error().kind == ErrorKind::AccessDenied);
    CHECK(f.count("SELECT count(*) FROM audit_entry WHERE action = 'DOCTOR_SCHEDULE_SET' AND outcome = 'DENIED'") == 1);

    // A receptionist's account cannot be given a doctor profile (service and store).
    const auto recep = f.addStaff("frontdesk", "receptionist");
    auto notDoctor = f.appointments->setDoctorSchedule(test::admin(), recep, ok);
    REQUIRE_FALSE(notDoctor.ok());
    CHECK(notDoctor.error().kind == ErrorKind::NotFound);
    CHECK_THROWS_AS(f.db().exec("INSERT INTO doctor_profile (staff_id, department, updated_at) VALUES (" +
                                std::to_string(recep) + ", 'X', 'now')"),
                    DbError);
}

// ------------------------------------------------------------ TC-App-01 (HMS-F-010)

TEST_CASE("TC-App-01 (1) booking a free slot marks it booked and returns the confirmation") {
    AppointmentFixture f;
    const auto doc = f.addDoctor("mehta");
    const auto uhid = f.addPatient("Ravi Kumar");

    auto before = f.appointments->freeSlots(test::receptionist(), doc, kMonday);
    REQUIRE(before.ok());
    CHECK(hasSlot(before.value(), at(kMonday, 9, 0)));

    const auto a = f.bookOk(uhid, doc, at(kMonday, 9, 0));
    CHECK(a.id == 1);
    CHECK(a.uhid == uhid);
    CHECK(a.status == AppointmentStatus::Booked);
    CHECK(a.start == at(kMonday, 9, 0));
    CHECK(a.slotMinutes == 15);
    CHECK(a.doctorUsername == "mehta");

    auto after = f.appointments->freeSlots(test::receptionist(), doc, kMonday);
    REQUIRE(after.ok());
    CHECK_FALSE(hasSlot(after.value(), at(kMonday, 9, 0)));
    CHECK(after.value().size() == before.value().size() - 1);

    // Audit: the appointment number only, never patient data.
    CHECK(f.count("SELECT count(*) FROM audit_entry WHERE action = 'APPOINTMENT_BOOK' AND outcome = 'OK' "
                  "AND record_id = 'APT-000001' AND user_id = 'STAFF-R1'") == 1);
    CHECK(f.count("SELECT count(*) FROM audit_entry WHERE ifnull(record_id,'') LIKE '%Ravi%' "
                  "OR ifnull(changed_fields,'') LIKE '%Ravi%'") == 0);
}

TEST_CASE("TC-App-01 (2) only real, future slots inside the doctor's hours can be booked") {
    AppointmentFixture f;
    const auto doc = f.addDoctor("mehta");
    const auto uhid = f.addPatient("Ravi Kumar");
    auto& svc = *f.appointments;

    // Today (11:30 now): slots that have started are not offered or accepted.
    auto today = svc.freeSlots(test::receptionist(), doc, kToday);
    REQUIRE(today.ok());
    REQUIRE_FALSE(today.value().empty());
    CHECK(today.value().front() == at(kToday, 11, 45));
    auto started = svc.book(test::receptionist(), uhid, doc, at(kToday, 11, 30));
    REQUIRE_FALSE(started.ok());
    CHECK(started.error().describe().find("has already started") != std::string::npos);

    auto offGrid = svc.book(test::receptionist(), uhid, doc, at(kMonday, 9, 5));
    REQUIRE_FALSE(offGrid.ok());
    CHECK(offGrid.error().kind == ErrorKind::Validation);
    CHECK(offGrid.error().describe().find("not a slot start") != std::string::npos);

    auto outside = svc.book(test::receptionist(), uhid, doc, at(kMonday, 13, 0));  // hours end at 13:00
    REQUIRE_FALSE(outside.ok());
    CHECK(outside.error().kind == ErrorKind::Validation);

    auto past = svc.book(test::receptionist(), uhid, doc, at(Date{2026, 10, 24}, 9, 0));
    REQUIRE_FALSE(past.ok());
    CHECK(past.error().describe().find("in the past") != std::string::npos);

    const Date tooFar = civilFromDays(daysFromCivil(2026, 10, 25) + 91);
    auto far = svc.book(test::receptionist(), uhid, doc, at(tooFar, 9, 0));
    REQUIRE_FALSE(far.ok());
    CHECK(far.error().describe().find("90 days ahead") != std::string::npos);
    const Date lastDay = civilFromDays(daysFromCivil(2026, 10, 25) + 90);
    CHECK(svc.book(test::receptionist(), uhid, doc, at(lastDay, 9, 0)).ok());

    auto noPatient = svc.book(test::receptionist(), 999, doc, at(kMonday, 9, 0));
    REQUIRE_FALSE(noPatient.ok());
    CHECK(noPatient.error().kind == ErrorKind::NotFound);

    auto noDoctor = svc.book(test::receptionist(), uhid, 999, at(kMonday, 9, 0));
    REQUIRE_FALSE(noDoctor.ok());
    CHECK(noDoctor.error().kind == ErrorKind::NotFound);

    CHECK(f.count("SELECT count(*) FROM appointment") == 1);  // only the last-day booking
}

// ------------------------------------------------------------ TC-App-02 (HMS-F-011)

TEST_CASE("TC-App-02 (1) a booked slot is refused with a clear message and the existing appointment is unchanged") {
    AppointmentFixture f;
    const auto doc = f.addDoctor("mehta");
    const auto first = f.addPatient("Ravi Kumar");
    const auto second = f.addPatient("Anita Rao");
    const auto a = f.bookOk(first, doc, at(kMonday, 9, 0));

    auto r = f.appointments->book(test::receptionist(), second, doc, at(kMonday, 9, 0));
    REQUIRE_FALSE(r.ok());
    CHECK(r.error().kind == ErrorKind::Conflict);
    CHECK(r.error().message.find("already has an appointment from 09:00 to 09:15") != std::string::npos);
    CHECK(r.error().message.find("Nothing was saved") != std::string::npos);

    CHECK(f.count("SELECT count(*) FROM appointment") == 1);
    CHECK(f.count("SELECT uhid FROM appointment WHERE appointment_id = " + std::to_string(a.id)) == first);
    CHECK(f.count("SELECT count(*) FROM appointment WHERE status = 'BOOKED'") == 1);
    CHECK(f.count("SELECT count(*) FROM audit_entry WHERE action = 'APPOINTMENT_BOOK'") == 1);
}

TEST_CASE("TC-App-02 (2) the store itself refuses a second booked appointment at the same doctor and start") {
    AppointmentFixture f;
    const auto doc = f.addDoctor("mehta");
    const auto first = f.addPatient("Ravi Kumar");
    const auto second = f.addPatient("Anita Rao");
    f.bookOk(first, doc, at(kMonday, 9, 0));

    const std::string insert =
        "INSERT INTO appointment (uhid, doctor_staff_id, slot_start, slot_minutes, status, created_at, created_by) "
        "VALUES (" + std::to_string(second) + ", " + std::to_string(doc) +
        ", '2026-10-26T09:00', 15, 'BOOKED', 'now', 'test')";
    CHECK_THROWS_AS(f.db().exec(insert), DbError);
    // A cancelled row at the same start is allowed: cancelling frees the slot.
    CHECK_NOTHROW(f.db().exec(
        "INSERT INTO appointment (uhid, doctor_staff_id, slot_start, slot_minutes, status, created_at, created_by, "
        "cancelled_at, cancelled_by) VALUES (" + std::to_string(second) + ", " + std::to_string(doc) +
        ", '2026-10-26T09:00', 15, 'CANCELLED', 'now', 'test', 'now', 'test')"));
}

TEST_CASE("TC-App-02 (3) overlapping periods are refused even when the starts differ") {
    AppointmentFixture f;
    const auto doc = f.addDoctor("mehta");
    const auto first = f.addPatient("Ravi Kumar");
    const auto second = f.addPatient("Anita Rao");
    f.bookOk(first, doc, at(kMonday, 9, 0));  // 09:00-09:15

    // The Admin moves Monday's start to 09:10: the new grid has a 09:10 slot
    // that overlaps the existing 09:00-09:15 appointment.
    DoctorScheduleDraft d;
    d.department = "General Medicine";
    for (int day = 1; day <= 7; ++day) d.hours.push_back(WeeklyHours{day, day == 1 ? 9 * 60 + 10 : 9 * 60, 13 * 60});
    auto saved = f.appointments->setDoctorSchedule(test::admin(), doc, d);
    REQUIRE(saved.ok());
    CHECK(saved.value().bookedOutsideHours == 1);  // kept; reception is told to reschedule it

    auto slots = f.appointments->freeSlots(test::receptionist(), doc, kMonday);
    REQUIRE(slots.ok());
    CHECK_FALSE(hasSlot(slots.value(), at(kMonday, 9, 10)));
    CHECK(slots.value().front() == at(kMonday, 9, 25));

    auto r = f.appointments->book(test::receptionist(), second, doc, at(kMonday, 9, 10));
    REQUIRE_FALSE(r.ok());
    CHECK(r.error().kind == ErrorKind::Conflict);
}

TEST_CASE("TC-App-02 (4) a patient cannot hold two appointments at the same time") {
    AppointmentFixture f;
    const auto mehta = f.addDoctor("mehta");
    const auto rao = f.addDoctor("rao", "Dermatology");
    const auto uhid = f.addPatient("Ravi Kumar");
    f.bookOk(uhid, mehta, at(kMonday, 9, 0));

    auto r = f.appointments->book(test::receptionist(), uhid, rao, at(kMonday, 9, 0));
    REQUIRE_FALSE(r.ok());
    CHECK(r.error().kind == ErrorKind::Conflict);
    CHECK(r.error().message.find("This patient already has an appointment") != std::string::npos);
    CHECK(f.bookOk(uhid, rao, at(kMonday, 9, 15)).id == 2);  // the next slot is fine
}

TEST_CASE("TC-App-02 (5) NF-002 a failure before commit saves nothing and the slot stays free") {
    AppointmentFixture f;
    const auto doc = f.addDoctor("mehta");
    const auto uhid = f.addPatient("Ravi Kumar");
    f.db().setFaultHook([](const char* point) {
        if (std::string(point) == "appointment_before_commit") throw InjectedFault(point);
    });
    auto r = f.appointments->book(test::receptionist(), uhid, doc, at(kMonday, 9, 0));
    f.db().setFaultHook(nullptr);
    REQUIRE_FALSE(r.ok());
    CHECK(r.error().kind == ErrorKind::StorageFailure);
    CHECK(f.count("SELECT count(*) FROM appointment") == 0);
    CHECK(f.count("SELECT count(*) FROM audit_entry WHERE action = 'APPOINTMENT_BOOK'") == 0);
    CHECK(f.bookOk(uhid, doc, at(kMonday, 9, 0)).start == at(kMonday, 9, 0));
}

TEST_CASE("TC-App-02 (6) SR-001 only a Receptionist may book; a denied attempt is audited") {
    AppointmentFixture f;
    const auto doc = f.addDoctor("mehta");
    const auto uhid = f.addPatient("Ravi Kumar");
    for (const Session& s : {test::doctor(), test::admin(), test::pharmacist()}) {
        auto r = f.appointments->book(s, uhid, doc, at(kMonday, 9, 0));
        REQUIRE_FALSE(r.ok());
        CHECK(r.error().kind == ErrorKind::AccessDenied);
    }
    CHECK(f.count("SELECT count(*) FROM appointment") == 0);
    CHECK(f.count("SELECT count(*) FROM audit_entry WHERE action = 'APPOINTMENT_BOOK' AND outcome = 'DENIED'") == 3);
    CHECK_FALSE(f.appointments->doctorsOn(test::doctor(), kMonday).ok());
}

// ------------------------------------------------------------ TC-App-03 (HMS-F-012)

TEST_CASE("TC-App-03 (1) cancelling releases the slot at once") {
    AppointmentFixture f;
    const auto doc = f.addDoctor("mehta");
    const auto first = f.addPatient("Ravi Kumar");
    const auto second = f.addPatient("Anita Rao");
    const auto a = f.bookOk(first, doc, at(kMonday, 9, 0));

    auto c = f.appointments->cancel(test::receptionist(), a.id);
    REQUIRE(c.ok());
    CHECK(c.value().status == AppointmentStatus::Cancelled);
    auto slots = f.appointments->freeSlots(test::receptionist(), doc, kMonday);
    REQUIRE(slots.ok());
    CHECK(hasSlot(slots.value(), at(kMonday, 9, 0)));
    CHECK(f.bookOk(second, doc, at(kMonday, 9, 0)).uhid == second);

    auto again = f.appointments->cancel(test::receptionist(), a.id);
    REQUIRE_FALSE(again.ok());
    CHECK(again.error().kind == ErrorKind::Conflict);
    CHECK(again.error().message.find("is cancelled and cannot be cancelled") != std::string::npos);
    CHECK(f.count("SELECT count(*) FROM audit_entry WHERE action = 'APPOINTMENT_CANCEL' AND outcome = 'OK' "
                  "AND record_id = 'APT-000001'") == 1);
    CHECK(f.appointments->cancel(test::receptionist(), 999).error().kind == ErrorKind::NotFound);
}

TEST_CASE("TC-App-03 (2) rescheduling cancels the old appointment, links it to the new one, and frees its slot") {
    AppointmentFixture f;
    const auto doc = f.addDoctor("mehta");
    const auto uhid = f.addPatient("Ravi Kumar");
    const auto a = f.bookOk(uhid, doc, at(kMonday, 9, 0));

    auto r = f.appointments->reschedule(test::receptionist(), a.id, at(kTuesday, 10, 30));
    REQUIRE(r.ok());
    CHECK(r.value().id == 2);
    CHECK(r.value().start == at(kTuesday, 10, 30));
    CHECK(r.value().uhid == uhid);
    CHECK(r.value().doctorStaffId == doc);

    CHECK(f.count("SELECT count(*) FROM appointment WHERE appointment_id = 1 AND status = 'CANCELLED' "
                  "AND replaced_by = 2") == 1);
    auto monday = f.appointments->freeSlots(test::receptionist(), doc, kMonday);
    REQUIRE(monday.ok());
    CHECK(hasSlot(monday.value(), at(kMonday, 9, 0)));

    // Audit: the cancel of the old one, and the new one linked to it.
    CHECK(f.count("SELECT count(*) FROM audit_entry WHERE action = 'APPOINTMENT_CANCEL' AND record_id = 'APT-000001' "
                  "AND reason_code = 'RESCHEDULED'") == 1);
    CHECK(f.count("SELECT count(*) FROM audit_entry r JOIN audit_entry c ON r.related_event_id = c.event_id "
                  "WHERE r.action = 'APPOINTMENT_RESCHEDULE' AND r.record_id = 'APT-000002' "
                  "AND c.record_id = 'APT-000001'") == 1);

    // Moving by one slot within its own period is fine (the old one is ignored).
    auto shift = f.appointments->reschedule(test::receptionist(), 2, at(kTuesday, 10, 45));
    CHECK(shift.ok());
    auto same = f.appointments->reschedule(test::receptionist(), 3, at(kTuesday, 10, 45));
    REQUIRE_FALSE(same.ok());
    CHECK(same.error().kind == ErrorKind::Validation);
}

TEST_CASE("TC-App-03 (3) the cutoff: allowed exactly at the cutoff, refused one minute inside it") {
    AppointmentFixture f;
    const auto doc = f.addDoctor("mehta");
    const auto uhid = f.addPatient("Ravi Kumar");
    const auto a = f.bookOk(uhid, doc, at(kMonday, 9, 0));
    const auto b = f.bookOk(uhid, doc, at(kMonday, 9, 30));

    f.setLocalTime(kMonday, 8, 1);  // 59 minutes before 09:00
    auto late = f.appointments->cancel(test::receptionist(), a.id);
    REQUIRE_FALSE(late.ok());
    CHECK(late.error().kind == ErrorKind::Conflict);
    CHECK(late.error().message.find("changes close 60 minutes before the start") != std::string::npos);
    auto lateMove = f.appointments->reschedule(test::receptionist(), a.id, at(kMonday, 12, 0));
    REQUIRE_FALSE(lateMove.ok());
    CHECK(lateMove.error().kind == ErrorKind::Conflict);
    CHECK(f.count("SELECT count(*) FROM appointment WHERE status = 'BOOKED'") == 2);

    f.setLocalTime(kMonday, 8, 30);  // exactly 60 minutes before 09:30
    CHECK(f.appointments->cancel(test::receptionist(), b.id).ok());
}

TEST_CASE("TC-App-03 (4) NF-002 a failed reschedule changes nothing") {
    AppointmentFixture f;
    const auto doc = f.addDoctor("mehta");
    const auto uhid = f.addPatient("Ravi Kumar");
    const auto a = f.bookOk(uhid, doc, at(kMonday, 9, 0));
    f.db().setFaultHook([](const char* point) {
        if (std::string(point) == "reschedule_before_commit") throw InjectedFault(point);
    });
    auto r = f.appointments->reschedule(test::receptionist(), a.id, at(kTuesday, 9, 0));
    f.db().setFaultHook(nullptr);
    REQUIRE_FALSE(r.ok());
    CHECK(r.error().kind == ErrorKind::StorageFailure);
    CHECK(f.count("SELECT count(*) FROM appointment") == 1);
    CHECK(f.count("SELECT count(*) FROM appointment WHERE status = 'BOOKED' AND replaced_by IS NULL") == 1);
    CHECK(f.count("SELECT count(*) FROM audit_entry WHERE action LIKE 'APPOINTMENT_%' AND action <> 'APPOINTMENT_BOOK'") == 0);
}

TEST_CASE("TC-App-03 (5) a patient's upcoming list shows booked appointments that have not started") {
    AppointmentFixture f;
    const auto doc = f.addDoctor("mehta");
    const auto uhid = f.addPatient("Ravi Kumar");
    f.bookOk(uhid, doc, at(kTuesday, 9, 0));
    f.bookOk(uhid, doc, at(kMonday, 10, 0));
    const auto cancelled = f.bookOk(uhid, doc, at(kMonday, 11, 0));
    REQUIRE(f.appointments->cancel(test::receptionist(), cancelled.id).ok());

    auto list = f.appointments->upcomingForPatient(test::receptionist(), uhid);
    REQUIRE(list.ok());
    REQUIRE(list.value().size() == 2);
    CHECK(list.value()[0].start == at(kMonday, 10, 0));
    CHECK(list.value()[1].start == at(kTuesday, 9, 0));
}

// ------------------------------------------------------------ TC-App-04 (HMS-F-013)

TEST_CASE("TC-App-04 (1) one reminder entry for every booked appointment in the next 48 hours") {
    AppointmentFixture f;
    const auto doc = f.addDoctor("mehta");
    const auto uhid = f.addPatient("Ravi Kumar");
    const auto other = f.addPatient("Anita Rao");
    f.bookOk(uhid, doc, at(kMonday, 9, 0));               // in the window
    f.bookOk(other, doc, at(kTuesday, 11, 30));           // exactly 48 hours ahead: in
    f.bookOk(uhid, doc, at(kTuesday, 11, 45));            // after the window
    const auto gone = f.bookOk(other, doc, at(kMonday, 10, 0));
    REQUIRE(f.appointments->cancel(test::receptionist(), gone.id).ok());  // cancelled: not reminded

    auto first = f.appointments->remindersDue(test::receptionist());
    REQUIRE(first.ok());
    REQUIRE(first.value().size() == 2);
    CHECK(first.value()[0].appointment.start == at(kMonday, 9, 0));
    CHECK(first.value()[1].appointment.start == at(kTuesday, 11, 30));
    CHECK(first.value()[0].newlyGenerated);
    CHECK(first.value()[1].newlyGenerated);
    CHECK(f.count("SELECT count(*) FROM appointment_reminder") == 2);
    CHECK(f.count("SELECT count(*) FROM audit_entry WHERE action = 'APPOINTMENT_REMINDER' AND outcome = 'OK'") == 2);

    // Running it again lists them without creating new entries.
    auto second = f.appointments->remindersDue(test::receptionist());
    REQUIRE(second.ok());
    REQUIRE(second.value().size() == 2);
    CHECK_FALSE(second.value()[0].newlyGenerated);
    CHECK(f.count("SELECT count(*) FROM appointment_reminder") == 2);

    // A day later the third appointment enters the window.
    f.setLocalTime(kMonday, 11, 30);
    auto later = f.appointments->remindersDue(test::receptionist());
    REQUIRE(later.ok());
    CHECK(f.count("SELECT count(*) FROM appointment_reminder") == 3);

    CHECK(f.appointments->remindersDue(test::doctor()).error().kind == ErrorKind::AccessDenied);
}

// ------------------------------------------------------------ TC-Doc-02 (HMS-F-021)

TEST_CASE("TC-Doc-02 (1) a doctor's queue is in time order and holds only that doctor's patients") {
    AppointmentFixture f;
    const auto mehta = f.addDoctor("mehta");
    const auto rao = f.addDoctor("rao", "Dermatology");
    const auto p1 = f.addPatient("Ravi Kumar");
    const auto p2 = f.addPatient("Anita Rao");
    const auto p3 = f.addPatient("John Dsouza");
    f.bookOk(p1, mehta, at(kMonday, 10, 0));
    f.bookOk(p2, mehta, at(kMonday, 9, 0));
    f.bookOk(p3, rao, at(kMonday, 9, 0));
    const auto cancelled = f.bookOk(p3, mehta, at(kMonday, 11, 0));
    REQUIRE(f.appointments->cancel(test::receptionist(), cancelled.id).ok());
    f.bookOk(p3, mehta, at(kTuesday, 9, 0));  // another day

    auto q = f.appointments->myQueue(test::doctorSession("mehta"), kMonday);
    REQUIRE(q.ok());
    REQUIRE(q.value().size() == 2);
    CHECK(q.value()[0].start == at(kMonday, 9, 0));
    CHECK(q.value()[0].patientName == "Anita Rao");
    CHECK(q.value()[1].start == at(kMonday, 10, 0));
    for (const auto& a : q.value()) CHECK(a.doctorStaffId == mehta);

    auto other = f.appointments->myQueue(test::doctorSession("RAO"), kMonday);  // usernames ignore case
    REQUIRE(other.ok());
    REQUIRE(other.value().size() == 1);
    CHECK(other.value()[0].uhid == p3);
}

TEST_CASE("TC-Doc-02 (2) only a Doctor has a queue") {
    AppointmentFixture f;
    f.addDoctor("mehta");
    auto r = f.appointments->myQueue(test::receptionist(), kMonday);
    REQUIRE_FALSE(r.ok());
    CHECK(r.error().kind == ErrorKind::AccessDenied);
    CHECK(f.count("SELECT count(*) FROM audit_entry WHERE action = 'APPOINTMENT_QUEUE_VIEW' AND outcome = 'DENIED'") == 1);
    auto unknown = f.appointments->myQueue(test::doctorSession("nobody"), kMonday);
    REQUIRE(unknown.ok());
    CHECK(unknown.value().empty());
}
