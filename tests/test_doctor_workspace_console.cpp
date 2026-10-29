// Issue #5: scripted console sessions for the Doctor menu, typed exactly as an
// operator would (STP 5.2, console-session tests).
#include <doctest.h>

#include <sstream>

#include "consultation_test_support.hpp"
#include "ui/console_app.hpp"

using namespace hms;
using test::at;
using test::ConsultationFixture;
using test::doctorSession;
using test::kMonday;

namespace {
std::string run(ConsultationFixture& f, const std::string& script, Session s, bool workspace = true) {
    std::istringstream in(script);
    std::ostringstream out;
    ConsoleIO io(in, out);
    ConsoleApp app(io, *f.service, *f.appointments, s);
    if (workspace) app.setConsultationService(f.consultations);
    app.run();
    return out.str();
}
bool contains(const std::string& hay, const std::string& needle) { return hay.find(needle) != std::string::npos; }
}  // namespace

TEST_CASE("Console TC-Doc-03: the doctor records a consultation through the menu") {
    ConsultationFixture f;
    const auto doc = f.addDoctor("mehta");
    const auto uhid = f.addPatient("Ravi Kumar");
    const auto appt = f.visit(uhid, doc);
    const std::string out = run(f,
                                "5\n"
                                "apt-x\n"                                   // not an appointment number -> asked again
                                + formatAppointmentId(appt.id) + "\n"
                                "\n"                                        // empty diagnosis -> asked again
                                "Viral fever\n"
                                "Y\n"                                       // prescribe
                                "Paracetamol 500\n"
                                "1 tablet twice a day\n"
                                "0\n"                                       // bad quantity -> asked again
                                "10\n"
                                "N\n"                                       // no more medicines
                                "Y\n"                                       // recommend a test
                                "Complete Blood Count\n"
                                "Fasting\n"
                                "N\n"
                                "Y\n"                                       // save
                                "0\n",
                                doctorSession("mehta"));
    CHECK(contains(out, "5. Record a consultation"));
    CHECK(contains(out, "Appointment: must look like APT-000012 or 12"));
    CHECK(contains(out, "Diagnosis: is required"));
    CHECK(contains(out, "Saved as CON-000001. The appointment is now Completed."));
    CHECK(contains(out, "Patient   : Ravi Kumar (HMS-000001)"));
    CHECK(contains(out, "Medicine  : Paracetamol 500 - 1 tablet twice a day - quantity 10"));
    CHECK(contains(out, "Test      : Complete Blood Count (Fasting)"));
    CHECK(f.count("SELECT count(*) FROM consultation") == 1);
    CHECK(f.count("SELECT count(*) FROM appointment WHERE status = 'COMPLETED'") == 1);
}

TEST_CASE("Console TC-Doc-03: declining the confirmation saves nothing") {
    ConsultationFixture f;
    const auto doc = f.addDoctor("mehta");
    const auto appt = f.visit(f.addPatient("Ravi Kumar"), doc);
    const std::string out = run(f, "5\n" + formatAppointmentId(appt.id) + "\nCough\nN\nN\nN\n0\n", doctorSession("mehta"));
    CHECK(contains(out, "Cancelled. Nothing was saved."));
    CHECK(f.count("SELECT count(*) FROM consultation") == 0);
    CHECK(f.count("SELECT count(*) FROM appointment WHERE status = 'BOOKED'") == 1);
}

TEST_CASE("Console TC-Doc-03: another doctor's appointment is refused with a clear message") {
    ConsultationFixture f;
    const auto doc = f.addDoctor("mehta");
    f.addDoctor("rao");
    const auto appt = f.visit(f.addPatient("Ravi Kumar"), doc);
    const std::string out = run(f, "5\n" + formatAppointmentId(appt.id) + "\nCough\nN\nN\nY\n0\n", doctorSession("rao"));
    CHECK(contains(out, "belongs to another doctor"));
    CHECK(f.count("SELECT count(*) FROM consultation") == 0);
}

TEST_CASE("Console TC-Doc-02 and history: weekly queue and a patient's past consultations") {
    ConsultationFixture f;
    const auto doc = f.addDoctor("mehta");
    const auto uhid = f.addPatient("Ravi Kumar");
    const auto appt = f.visit(uhid, doc);
    REQUIRE(f.consultations.recordConsultation(doctorSession("mehta"), f.draft(appt.id, "Cough")).ok());
    f.bookOk(uhid, doc, at(Date{2026, 10, 27}, 10, 0));
    const std::string out = run(f, "4\n26-10-2026\n6\nHMS-000001\n6\n999\n0\n", doctorSession("mehta"));
    CHECK(contains(out, "4. My appointments for the week"));
    CHECK(contains(out, "MY APPOINTMENTS - mehta - Mon 26-10-2026 to Sun 01-11-2026"));
    CHECK(contains(out, "Tue 27-10-2026"));
    CHECK(contains(out, "2 appointments."));
    CHECK(contains(out, "PAST CONSULTATIONS of HMS-000001"));
    CHECK(contains(out, "Diagnosis : Cough"));
    CHECK(contains(out, "No patient found with UHID"));
}

TEST_CASE("Console TC-Doc-01: the doctor changes their own weekly hours; the menu is unchanged without the workspace") {
    ConsultationFixture f;
    f.addDoctor("mehta");
    // Monday 10:00-12:00, Tuesday off, the rest kept.
    const std::string out = run(f, "7\n10:00-12:00\noff\n\n\n\n\n\nY\n0\n", doctorSession("mehta"));
    CHECK(contains(out, "MY WEEKLY WORKING HOURS - Dr mehta"));
    CHECK(contains(out, "Saved. The booking screen uses these hours from now on."));
    CHECK(f.count("SELECT start_minute FROM doctor_availability WHERE weekday = 1") == 600);
    CHECK(f.count("SELECT count(*) FROM doctor_availability WHERE weekday = 2") == 0);
    CHECK(f.count("SELECT count(*) FROM doctor_availability") == 6);

    // Without the workspace, option 4 is not on the Doctor menu (issue #4 behaviour).
    const std::string plain = run(f, "4\n0\n", doctorSession("mehta"), false);
    CHECK(contains(plain, "Please choose 0, 1, 2 or 3."));
    CHECK_FALSE(contains(plain, "Record a consultation"));
}

TEST_CASE("Console: the Receptionist menu has no doctor workspace entries") {
    ConsultationFixture f;
    const std::string out = run(f, "5\n0\n", test::receptionist());
    CHECK_FALSE(contains(out, "Record a consultation"));
}
