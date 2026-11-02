// Issue #6: scripted console sessions for the medical history screen of the
// Doctor menu, typed exactly as an operator would (STP 5.2, console-session tests).
#include <doctest.h>

#include <sstream>

#include "medical_record_test_support.hpp"
#include "ui/console_app.hpp"

using namespace hms;
using test::at;
using test::doctorSession;
using test::kMonday;
using test::MedicalRecordFixture;

namespace {
std::string run(MedicalRecordFixture& f, const std::string& script, Session s, bool records = true) {
    std::istringstream in(script);
    std::ostringstream out;
    ConsoleIO io(in, out);
    ConsoleApp app(io, *f.service, *f.appointments, s);
    app.setConsultationService(f.consultations);
    if (records) app.setMedicalRecordService(f.records);
    app.run();
    return out.str();
}
bool contains(const std::string& hay, const std::string& needle) { return hay.find(needle) != std::string::npos; }
}  // namespace

TEST_CASE("Console TC-Rec-01: the doctor records a first version, then updates one field") {
    MedicalRecordFixture f;
    const auto doc = f.addDoctor("mehta");
    f.bookOk(f.addPatient("Ravi Kumar"), doc, at(kMonday, 9, 0));
    const std::string out = run(f,
                                "8\n"
                                "hms-x\n"                     // not a UHID -> asked again
                                "HMS-000001\n"
                                "1\n"                         // update
                                "Type 2 diabetes\n"
                                "Penicillin\n"
                                "Metformin 500\n"
                                "Y\n"                         // save
                                "1\n"                         // update again
                                "\n"                          // keep the history
                                "Penicillin, sulfa drugs\n"
                                "-\n"                         // clear the medications
                                "Y\n"
                                "2\n"                         // earlier versions
                                "\n"                          // back to the menu
                                "0\n",
                                doctorSession("mehta"));
    CHECK(contains(out, "8. Medical history, allergies and current medications of a patient"));
    CHECK(contains(out, "UHID: must look like HMS-000123 or 123"));
    CHECK(contains(out, "MEDICAL HISTORY of Ravi Kumar (HMS-000001)"));
    CHECK(contains(out, "No medical history recorded."));
    CHECK(contains(out, "Saved as MHV-000001."));
    CHECK(contains(out, "Saved as MHV-000002."));
    CHECK(contains(out, "Allergies           : Penicillin, sulfa drugs"));
    CHECK(contains(out, "Current medications : (none recorded)  (maintained by the doctor)"));
    CHECK(contains(out, "2 versions in all."));
    CHECK(contains(out, "EARLIER VERSIONS (newest first)"));
    CHECK(contains(out, "Current medications : Metformin 500  (maintained by the doctor)"));
    CHECK(f.count("SELECT count(*) FROM medical_history_version") == 2);
    CHECK(f.count("SELECT count(*) FROM medical_history_version WHERE version_id = 2 AND history = 'Type 2 diabetes' "
                  "AND current_medications = ''") == 1);
}

TEST_CASE("Console TC-Rec-01: declining the confirmation saves nothing") {
    MedicalRecordFixture f;
    const auto doc = f.addDoctor("mehta");
    f.bookOk(f.addPatient("Ravi Kumar"), doc, at(kMonday, 9, 0));
    const std::string out = run(f, "8\n1\n1\nAsthma\n\n\nN\n\n0\n", doctorSession("mehta"));
    CHECK(contains(out, "Cancelled. Nothing was saved."));
    CHECK(f.count("SELECT count(*) FROM medical_history_version") == 0);
}

TEST_CASE("Console TC-Rec-01: a doctor without an appointment with the patient is refused") {
    MedicalRecordFixture f;
    f.addDoctor("mehta");
    f.addPatient("Ravi Kumar");
    const std::string out = run(f, "8\n1\n0\n", doctorSession("mehta"));
    CHECK(contains(out, "no booked or completed appointment with patient HMS-000001"));
    CHECK_FALSE(contains(out, "MEDICAL HISTORY of"));
}

TEST_CASE("Console: without the medical record service the Doctor menu stops at 7") {
    MedicalRecordFixture f;
    f.addDoctor("mehta");
    const std::string out = run(f, "8\n0\n", doctorSession("mehta"), false);
    CHECK_FALSE(contains(out, "8. Medical history"));
    CHECK(contains(out, "Please choose a number from 0 to 7."));
}
