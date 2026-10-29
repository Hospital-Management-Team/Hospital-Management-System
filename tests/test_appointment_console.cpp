// Issue #4: scripted console sessions for appointments, typed exactly as an
// operator would (STP 5.2, console-session tests).
#include <doctest.h>

#include <sstream>

#include "appointment_test_support.hpp"
#include "ui/console_app.hpp"
#include "ui/doctor_schedule_screen.hpp"

using namespace hms;
using test::AppointmentFixture;
using test::at;
using test::kMonday;
using test::kTuesday;

namespace {
std::string run(AppointmentFixture& f, const std::string& script, Session s = test::receptionist()) {
    std::istringstream in(script);
    std::ostringstream out;
    ConsoleIO io(in, out);
    ConsoleApp app(io, *f.service, *f.appointments, s);
    app.run();
    return out.str();
}
bool contains(const std::string& hay, const std::string& needle) { return hay.find(needle) != std::string::npos; }
}  // namespace

TEST_CASE("Console TC-App-01: reception books an appointment through the menu") {
    AppointmentFixture f;
    f.addDoctor("mehta", "Cardiology");
    f.addPatient("Ravi Kumar");
    const std::string out = run(f,
                                "5\n"            // Book
                                "HMS-000001\n"   // patient
                                "31-02-2026\n"   // impossible date -> asked again
                                "26-10-2026\n"   // Monday
                                "1\n"            // Dr mehta
                                "2\n"            // 09:15
                                "Y\n"
                                "0\n");
    CHECK(contains(out, "Patient: Ravi Kumar (HMS-000001)"));
    CHECK(contains(out, "not a real calendar date"));
    CHECK(contains(out, "Doctors working on Mon 26-10-2026"));
    CHECK(contains(out, "Cardiology"));
    CHECK(contains(out, "Appointment booked: APT-000001"));
    CHECK(contains(out, "Mon 26-10-2026 at 09:15, Dr mehta (Cardiology)"));
    CHECK(f.count("SELECT count(*) FROM appointment WHERE slot_start = '2026-10-26T09:15' AND status = 'BOOKED'") == 1);
}

TEST_CASE("Console TC-App-01: declining the confirmation books nothing; a day with no doctor is reported") {
    AppointmentFixture f;
    const auto doc = f.addStaff("mehta");
    DoctorScheduleDraft d;
    d.department = "ENT";
    d.hours = {WeeklyHours{1, 9 * 60, 10 * 60}};  // Mondays only
    REQUIRE(f.appointments->setDoctorSchedule(test::admin(), doc, d).ok());
    f.addPatient("Ravi Kumar");
    const std::string out = run(f, "5\n1\n27-10-2026\n26-10-2026\n1\n1\nN\n0\n");
    CHECK(contains(out, "No doctor works on Tue 27-10-2026"));
    CHECK(contains(out, "Cancelled. Nothing was changed."));
    CHECK(f.count("SELECT count(*) FROM appointment") == 0);
}

TEST_CASE("Console TC-App-03: reception cancels, then reschedules, through the menu") {
    AppointmentFixture f;
    const auto doc = f.addDoctor("mehta");
    const auto uhid = f.addPatient("Ravi Kumar");
    f.bookOk(uhid, doc, at(kMonday, 9, 0));
    f.bookOk(uhid, doc, at(kMonday, 10, 0));

    // Cancel the first upcoming one (09:00).
    std::string out = run(f, "6\n1\n1\nC\nY\n0\n");
    CHECK(contains(out, "APT-000001 is cancelled. The slot is free for other patients."));

    // Reschedule the remaining one (10:00) to Tuesday's first slot.
    out = run(f, "6\n1\n1\nR\n27-10-2026\n1\nY\n0\n");
    CHECK(contains(out, "Rescheduled. New appointment: APT-000003, Tue 27-10-2026 at 09:00."));
    CHECK(contains(out, "APT-000002 is cancelled and its slot is free again."));
    CHECK(f.count("SELECT count(*) FROM appointment WHERE status = 'BOOKED'") == 1);
    CHECK(f.count("SELECT replaced_by FROM appointment WHERE appointment_id = 2") == 3);
}

TEST_CASE("Console TC-App-03: inside the cutoff the change is refused with the reason") {
    AppointmentFixture f;
    const auto doc = f.addDoctor("mehta");
    const auto uhid = f.addPatient("Ravi Kumar");
    f.bookOk(uhid, doc, at(kMonday, 9, 0));
    f.setLocalTime(kMonday, 8, 30);
    const std::string out = run(f, "6\n1\n1\nC\nY\n0\n");
    CHECK(contains(out, "It can no longer be cancelled: changes close 60 minutes before the start."));
    CHECK(f.count("SELECT count(*) FROM appointment WHERE status = 'BOOKED'") == 1);
}

TEST_CASE("Console TC-App-04: the reminder list marks new reminders and shows the mobile number") {
    AppointmentFixture f;
    const auto doc = f.addDoctor("mehta");
    const auto uhid = f.addPatient("Ravi Kumar");
    f.bookOk(uhid, doc, at(kMonday, 9, 0));
    std::string out = run(f, "7\n7\n0\n");
    CHECK(contains(out, "APPOINTMENT REMINDERS - booked appointments in the next 48 hours"));
    CHECK(contains(out, "NEW"));
    CHECK(contains(out, "listed before"));
    CHECK(contains(out, "+91 9"));
    CHECK(contains(out, "1 new reminder recorded."));
    CHECK(contains(out, "0 new reminders recorded."));
}

TEST_CASE("Console TC-Doc-02: the Doctor menu shows the doctor's own queue and no booking options") {
    AppointmentFixture f;
    const auto doc = f.addDoctor("mehta");
    const auto uhid = f.addPatient("Ravi Kumar");
    f.bookOk(uhid, doc, at(kMonday, 9, 0));
    const std::string out = run(f, "3\n26-10-2026\n5\n0\n", test::doctorSession("mehta"));
    CHECK(contains(out, "3. My appointments (OPD queue)"));
    CHECK_FALSE(contains(out, "Book an appointment"));
    CHECK(contains(out, "MY APPOINTMENTS - mehta - Mon 26-10-2026"));
    CHECK(contains(out, "09:00"));
    CHECK(contains(out, "Ravi Kumar"));
    CHECK(contains(out, "1 appointment."));
    CHECK(contains(out, "Please choose 0, 1, 2 or 3."));  // option 5 is not on the Doctor menu
}

TEST_CASE("Console TC-Doc-01: the Admin sets a doctor's department and weekly hours") {
    AppointmentFixture f;
    f.addStaff("mehta");
    std::istringstream in(
        "1\n"                   // Dr mehta
        "\n"                    // department required (no current value)
        "Cardiology\n"
        "\n"                    // no specialization
        "09:00-13:00\n"         // Monday
        "13:00-09:00\n"         // Tuesday: end before start -> asked again
        "14:00-17:30\n"
        "\n\n\n"                // Wednesday-Friday: keep "off"
        "off\n"                 // Saturday
        "10:00-12:00\n"         // Sunday
        "Y\n");
    std::ostringstream out;
    ConsoleIO io(in, out);
    doctorScheduleScreen(io, *f.appointments, test::admin());
    const std::string text = out.str();
    CHECK(contains(text, "(not set)"));
    CHECK(contains(text, "Department: is required"));
    CHECK(contains(text, "the end time must be after the start time"));
    CHECK(contains(text, "Mon 09:00-13:00, Tue 14:00-17:30, Sun 10:00-12:00"));
    CHECK(contains(text, "Saved."));
    CHECK(f.count("SELECT count(*) FROM doctor_availability") == 3);
    CHECK(f.count("SELECT end_minute FROM doctor_availability WHERE weekday = 2") == 17 * 60 + 30);
}
