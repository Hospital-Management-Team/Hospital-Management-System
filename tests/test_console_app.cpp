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
    const std::string out = runScript(f, "1\nGeeta Bai\n\n70\nF\nVillage Road, Mandya\n\nY\n0\n");
    CHECK(contains(out, "estimated from age; about 70 years"));
    CHECK(contains(out, "no phone recorded"));
    CHECK(f.count("SELECT count(*) FROM patient WHERE dob_estimated = 1 AND date_of_birth = '1956-01-01'") == 1);
}

TEST_CASE("Console: declining the confirmation saves nothing") {
    test::StoreFixture f;
    const std::string out = runScript(f, "1\nRavi Kumar\n05-03-1990\nM\nAddr\n\nN\n0\n");
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
    const std::string out = runScript(f, "1\nravi kumar\n05-03-1990\nM\n12 MG Road\n\nY\n1\n0\n");
    CHECK(contains(out, "STRONG"));
    CHECK(contains(out, "No new record was created. Use the existing UHID HMS-000001"));
    CHECK(f.count("SELECT count(*) FROM patient") == 1);
}

TEST_CASE("Console: strong duplicate - twins registered with a reason code") {
    test::StoreFixture f;
    runScript(f, "1\nAnu Reddy\n10-10-2010\nF\n7 Lake View\n\nY\n0\n");
    const std::string out = runScript(f, "1\nAnu Reddy\n10-10-2010\nF\n7 Lake View\n\nY\nD\n1\n0\n");
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
