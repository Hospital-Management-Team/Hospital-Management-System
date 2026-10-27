// Scripted console sessions: the reception workflow end to end through the
// menus, exactly as an operator would type it.
#include <doctest.h>

#include <sstream>

#include "patient_test_support.hpp"
#include "ui/console_app.hpp"

using namespace hms;

namespace {
std::string runScript(test::StoreFixture& f, const std::string& script, Session s = test::receptionist()) {
    std::istringstream in(script);
    std::ostringstream out;
    ConsoleIO io(in, out);
    ConsoleApp app(io, *f.service, s);
    app.run();
    return out.str();
}
bool contains(const std::string& hay, const std::string& needle) { return hay.find(needle) != std::string::npos; }
}  // namespace

TEST_CASE("Console: register a patient, re-prompting for invalid fields, then find them") {
    test::StoreFixture f;
    const std::string script =
        "1\n"                    // Register
        "\n"                     // empty name -> error
        "Ravi Kumar\n"
        "31-02-1990\n"           // impossible date -> error
        "05-03-1990\n"
        "X\n"                    // bad gender -> error
        "M\n"
        "12 MG Road, Bengaluru\n"
        "12345\n"                // bad phone -> error
        "098765 43210\n"
        "Y\n"                    // save
        "2\n2\nravi\n\n"         // search by name, go back
        "0\n";
    const std::string out = runScript(f, script);
    CHECK(contains(out, "Full name: is required"));
    CHECK(contains(out, "not a real calendar date"));
    CHECK(contains(out, "Gender: must be M (Male), F (Female) or O (Other)"));
    CHECK(contains(out, "Mobile number: must be a 10-digit Indian mobile number"));
    CHECK(contains(out, "Mobile        : +91 98765 43210"));
    CHECK(contains(out, "Patient registered. UHID: HMS-000001"));
    CHECK(contains(out, "1 patient found."));
    CHECK(f.count("SELECT count(*) FROM patient") == 1);
}

TEST_CASE("Console: age instead of date of birth") {
    test::StoreFixture f;
    // An elderly patient: no date of birth, and the mobile number of the son who brought her.
    const std::string out = runScript(f, "1\nGeeta Bai\n\n70\nF\nVillage Road, Mandya\n9123456780\nY\n0\n");
    CHECK(contains(out, "estimated from age; about 70 years"));
    CHECK(contains(out, "Mobile        : +91 91234 56780"));
    CHECK(f.count("SELECT count(*) FROM patient WHERE dob_estimated = 1 AND date_of_birth = '1956-01-01'") == 1);
}

TEST_CASE("Console: the mobile number cannot be left empty") {
    test::StoreFixture f;
    const std::string out = runScript(f, "1\nRavi Kumar\n05-03-1990\nM\nAddr\n\n9123456790\nY\n0\n");
    CHECK(contains(out, "Mobile number: is required"));
    CHECK(contains(out, "Patient registered. UHID: HMS-000001"));
    CHECK(f.count("SELECT count(*) FROM patient WHERE phone = '9123456790' AND no_phone_confirmed = 0") == 1);
}

TEST_CASE("Console: no phone available - staff confirm it, and it is shown on the record") {
    test::StoreFixture f;
    // N, then "No" to the confirmation (asked again), then N and "Yes".
    const std::string out =
        runScript(f, "1\nMuniyamma\n\n75\nF\nNear bus stand, Kolar\nN\nN\nn\nY\nY\n3\n1\n0\n");
    CHECK(contains(out, "Confirm: the patient AND everyone with them have no mobile number?"));
    CHECK(contains(out, "Mobile        : No phone (confirmed at registration)"));
    CHECK(contains(out, "Patient registered. UHID: HMS-000001"));
    CHECK(f.count("SELECT count(*) FROM patient WHERE phone IS NULL AND no_phone_confirmed = 1") == 1);
}

TEST_CASE("Console: declining the confirmation saves nothing") {
    test::StoreFixture f;
    const std::string out = runScript(f, "1\nRavi Kumar\n05-03-1990\nM\nAddr\n9123456781\nN\n0\n");
    CHECK(contains(out, "Registration cancelled. Nothing was saved."));
    CHECK(f.count("SELECT count(*) FROM patient") == 0);
}

TEST_CASE("Console: end of input during registration cancels it and leaves the menus") {
    test::StoreFixture f;
    const std::string out = runScript(f, "1\nRavi Kumar\n");
    CHECK(contains(out, "Registration cancelled. Nothing was saved."));
    CHECK(contains(out, "Goodbye."));
    CHECK(f.count("SELECT count(*) FROM patient") == 0);
}

TEST_CASE("Console: shared phone number - family member registered as a different person") {
    test::StoreFixture f;
    runScript(f, "1\nLakshmi Devi\n01-06-1975\nF\n4 Temple St\n9876543210\nY\n0\n");
    const std::string out = runScript(f, "1\nArjun\n14-08-2015\nM\n4 Temple St\n+91 98765 43210\nY\nD\n0\n");
    CHECK(contains(out, "POSSIBLE EXISTING PATIENT FOUND"));
    CHECK(contains(out, "SAME PHONE"));
    CHECK(contains(out, "Patient registered as a different person. UHID: HMS-000002"));
    CHECK(f.count("SELECT count(*) FROM patient") == 2);
}

TEST_CASE("Console: strong duplicate - staff choose the existing patient") {
    test::StoreFixture f;
    runScript(f, "1\nRavi Kumar\n05-03-1990\nM\n12 MG Road\n9876543210\nY\n0\n");
    const std::string out = runScript(f, "1\nravi kumar\n05-03-1990\nM\n12 MG Road\n9123456782\nY\n1\n0\n");
    CHECK(contains(out, "STRONG"));
    CHECK(contains(out, "No new record was created. Use the existing UHID HMS-000001"));
    CHECK(f.count("SELECT count(*) FROM patient") == 1);
}

TEST_CASE("Console: strong duplicate - twins registered with a reason code") {
    test::StoreFixture f;
    runScript(f, "1\nAnu Reddy\n10-10-2010\nF\n7 Lake View\n9123456783\nY\n0\n");
    const std::string out = runScript(f, "1\nAnu Reddy\n10-10-2010\nF\n7 Lake View\n9123456783\nY\nD\n1\n0\n");
    CHECK(contains(out, "Why is this a different person?"));
    CHECK(contains(out, "UHID: HMS-000002"));
    CHECK(f.count("SELECT count(*) FROM audit_entry WHERE reason_code = 'TWINS_OR_MULTIPLE_BIRTH'") == 1);
}

TEST_CASE("Console: the Doctor menu has no registration and search shows demographics only") {
    test::StoreFixture f;
    runScript(f, "1\nRavi Kumar\n05-03-1990\nM\n12 MG Road\n9876543210\nY\n0\n");
    const std::string out = runScript(f, "1\n3\n98765 43210\n1\n0\n", test::doctor());
    CHECK(contains(out, "DOCTOR MENU"));
    CHECK_FALSE(contains(out, "Register a new patient"));
    CHECK(contains(out, "PATIENT RECORD (demographic details)"));
}

// ------------------------------------------------ returning patient (option 4)

namespace {
void seedFamily(test::StoreFixture& f) {
    // Mother and son share one number; the grandmother has no phone.
    REQUIRE(f.service->registerPatient(test::receptionist(), test::draft("Lakshmi Devi", "01-06-1975", "F",
                                       std::string("9876543210")), DuplicateDecision::none()).ok());
    auto son = f.service->registerPatient(test::receptionist(),
                                          test::draft("Arjun", "14-08-2015", "M", std::string("9876543210")),
                                          DuplicateDecision::none());
    REQUIRE(son.ok());
    DuplicateDecision d;
    d.kind = DuplicateDecision::Kind::DifferentPerson;
    for (const auto& c : son.value().candidates) d.reviewedUhids.push_back(c.patient.uhid);
    REQUIRE(f.service->registerPatient(test::receptionist(),
                                       test::draft("Arjun", "14-08-2015", "M", std::string("9876543210")), d).ok());
    REQUIRE(f.service->registerPatient(test::receptionist(), test::draftNoPhone("Kamala Bai", "03-02-1950", "F"),
                                       DuplicateDecision::none()).ok());
}
}  // namespace

TEST_CASE("Console: returning patient - the mobile number lists the family, staff pick the person") {
    test::StoreFixture f;
    seedFamily(f);
    const std::string out = runScript(f, "4\n+91 98765 43210\n2\n0\n");
    CHECK(contains(out, "This number is shared by 2 patients. Pick the person by name and date of birth."));
    CHECK(contains(out, "Lakshmi Devi"));
    CHECK(contains(out, "UHID          : HMS-000002"));
    CHECK(contains(out, "Name          : Arjun"));
}

TEST_CASE("Console: returning patient with no phone - found by name and date of birth") {
    test::StoreFixture f;
    seedFamily(f);
    const std::string out = runScript(f, "4\n\nkamala\n03-02-1950\n1\n0\n");
    CHECK(contains(out, "Search by name and date of birth instead:"));
    CHECK(contains(out, "UHID          : HMS-000003"));
    CHECK(contains(out, "Mobile        : No phone (confirmed at registration)"));
}

TEST_CASE("Console: returning patient - unknown number falls back to name and date of birth") {
    test::StoreFixture f;
    seedFamily(f);
    const std::string out = runScript(f, "4\n9000000001\nLakshmi\n01-06-1975\n1\n0\n");
    CHECK(contains(out, "No patient is registered with this number."));
    CHECK(contains(out, "UHID          : HMS-000001"));
}

TEST_CASE("Console: returning patient - not found anywhere points to registration") {
    test::StoreFixture f;
    seedFamily(f);
    const std::string out = runScript(f, "4\n\nNobody\n01-01-2000\n0\n");
    CHECK(contains(out, "No patient found with that name and date of birth."));
    CHECK(contains(out, "use option 1 to register them"));
}

TEST_CASE("Console: search menu option 4 - name and date of birth; tables show the phone status") {
    test::StoreFixture f;
    seedFamily(f);
    const std::string out = runScript(f, "2\n4\nbai\n03-02-1950\n\n0\n");
    CHECK(contains(out, "Kamala Bai"));
    CHECK(contains(out, "No phone"));
    CHECK(contains(out, "1 patient found."));
}
