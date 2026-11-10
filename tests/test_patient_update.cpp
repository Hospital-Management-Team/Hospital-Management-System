// HMS-F-005: a Receptionist corrects a patient's details (issue: edit patient demographics).
// Service tests against a real SQLite store, and scripted sessions of the Reception menu.
#include <doctest.h>

#include <sstream>

#include "patient_test_support.hpp"
#include "ui/console_app.hpp"

using namespace hms;
using test::draft;
using test::receptionist;

namespace {
struct UpdateFixture : test::StoreFixture {
    std::int64_t addPatient(const std::string& name, const std::string& dob = "05-03-1990", const std::string& gender = "M",
                            std::optional<std::string> phone = std::nullopt) {
        auto r = service->registerPatient(receptionist(), draft(name, dob, gender, phone), DuplicateDecision::none());
        REQUIRE(r.ok());
        REQUIRE(r.value().status == RegistrationOutcome::Status::Registered);
        return r.value().uhid;
    }
    std::string rowText(const std::string& sql) {
        auto st = db().prepare(sql);
        return st.step() ? st.getText(0) : std::string("<none>");
    }
};

std::string runReception(UpdateFixture& f, const std::string& script) {
    std::istringstream in(script);
    std::ostringstream out;
    ConsoleIO io(in, out);
    ConsoleApp app(io, *f.service, *f.appointments, receptionist());
    app.run();
    return out.str();
}
bool contains(const std::string& hay, const std::string& needle) { return hay.find(needle) != std::string::npos; }
}  // namespace

TEST_CASE("TC-Reg-03 the Receptionist corrects a patient's details; only the changed fields are audited") {
    UpdateFixture f;
    const auto uhid = f.addPatient("Ravi Kumar", "05-03-1990", "M", std::string("9876543210"));

    PatientDraft d = draft("Ravi K. Kumar", "05-03-1990", "M", std::string("9876543210"), "7 Park Street, Mysuru");
    auto r = f.service->updateDemographics(receptionist(), uhid, d);
    REQUIRE(r.ok());
    CHECK(r.value().changed());
    CHECK(r.value().changedFields == std::vector<std::string>{"full_name", "address"});
    CHECK(r.value().record.fullName == "Ravi K. Kumar");
    CHECK(r.value().record.address == "7 Park Street, Mysuru");
    CHECK(f.rowText("SELECT normalised_name FROM patient WHERE uhid = 1") == "ravi k. kumar");

    // Search finds the new name, not the old one.
    CHECK(f.service->searchPatients(receptionist(), SearchMode::Name, "k. kumar").value().rows.size() == 1);

    // One audit entry, naming the fields and never holding the new values.
    CHECK(f.count("SELECT count(*) FROM audit_entry WHERE action = 'PATIENT_UPDATE_DEMOGRAPHICS' AND outcome = 'OK' "
                  "AND record_id = 'HMS-000001'") == 1);
    CHECK(f.rowText("SELECT changed_fields FROM audit_entry WHERE action = 'PATIENT_UPDATE_DEMOGRAPHICS'")
              .find("full_name") != std::string::npos);
    CHECK(f.count("SELECT count(*) FROM audit_entry WHERE changed_fields LIKE '%Park Street%' OR "
                  "changed_fields LIKE '%Ravi K.%' OR record_id LIKE '%9876543210%'") == 0);
}

TEST_CASE("TC-Reg-03 typing the same details changes nothing and writes no audit entry") {
    UpdateFixture f;
    const auto uhid = f.addPatient("Ravi Kumar", "05-03-1990", "M", std::string("9876543210"));
    const auto before = f.count("SELECT count(*) FROM audit_entry");

    auto r = f.service->updateDemographics(receptionist(), uhid,
                                           draft("Ravi Kumar", "05-03-1990", "M", std::string("98765 43210")));
    REQUIRE(r.ok());
    CHECK_FALSE(r.value().changed());
    CHECK(f.count("SELECT count(*) FROM audit_entry") == before);
}

TEST_CASE("TC-Reg-03 the mobile number rule still holds, and an estimated date of birth stays an estimate") {
    UpdateFixture f;
    const auto exact = f.addPatient("Ravi Kumar", "05-03-1990", "M", std::string("9876543210"));

    // No phone only with the confirmation flag; changing to "no phone" is allowed when confirmed.
    PatientDraft none = draft("Ravi Kumar", "05-03-1990", "M", std::string());
    none.noPhone = true;
    auto r = f.service->updateDemographics(receptionist(), exact, none);
    REQUIRE(r.ok());
    CHECK(r.value().changedFields == std::vector<std::string>{"phone"});
    CHECK(r.value().record.noPhoneConfirmed);
    CHECK_FALSE(r.value().record.phone.has_value());

    // A bad number is refused and nothing changes.
    auto bad = f.service->updateDemographics(receptionist(), exact, draft("Ravi Kumar", "05-03-1990", "M", std::string("12345")));
    REQUIRE_FALSE(bad.ok());
    CHECK(bad.error().kind == ErrorKind::Validation);

    // Registered by age: the date of birth is an estimate; typing the same date back keeps the flag.
    PatientDraft byAge = draft("Geeta Bai", "", "F");
    byAge.ageYears = "70";
    auto reg = f.service->registerPatient(receptionist(), byAge, DuplicateDecision::none());
    REQUIRE(reg.ok());
    const auto geeta = reg.value().uhid;
    auto same = f.service->updateDemographics(receptionist(), geeta,
                                              draft("Geeta Bai", "01-01-1956", "F", f.service->getPatient(receptionist(), geeta).value().phone));
    REQUIRE(same.ok());
    CHECK_FALSE(same.value().changed());
    CHECK(f.service->getPatient(receptionist(), geeta).value().dobEstimated);
}

TEST_CASE("TC-Reg-03 a change that would duplicate another patient is refused") {
    UpdateFixture f;
    f.addPatient("Ravi Kumar", "05-03-1990", "M");
    const auto other = f.addPatient("Ravi Kumaar", "05-03-1990", "M");  // a different spelling: not a duplicate

    auto r = f.service->updateDemographics(receptionist(), other,
                                           draft("Ravi Kumar", "05-03-1990", "M", f.service->getPatient(receptionist(), other).value().phone));
    REQUIRE_FALSE(r.ok());
    CHECK(r.error().kind == ErrorKind::Conflict);
    CHECK(f.rowText("SELECT full_name FROM patient WHERE uhid = 2") == "Ravi Kumaar");
    CHECK(f.count("SELECT count(*) FROM audit_entry WHERE action = 'PATIENT_UPDATE_DEMOGRAPHICS'") == 0);

    // Sharing a phone number with a family member stays allowed (the registration policy).
    const auto shared = f.service->getPatient(receptionist(), 1).value().phone;
    CHECK(f.service->updateDemographics(receptionist(), other, draft("Ravi Kumaar", "05-03-1990", "M", shared)).ok());
}

TEST_CASE("TC-Reg-03 only a Receptionist may change patient details; a refusal is audited") {
    UpdateFixture f;
    const auto uhid = f.addPatient("Ravi Kumar");
    const PatientDraft d = draft("Someone Else", "05-03-1990", "M");

    for (const Session& s : {test::doctor(), test::pharmacist()}) {
        auto r = f.service->updateDemographics(s, uhid, d);
        REQUIRE_FALSE(r.ok());
        CHECK(r.error().kind == ErrorKind::AccessDenied);
    }
    CHECK(f.count("SELECT count(*) FROM audit_entry WHERE action = 'PATIENT_UPDATE_DEMOGRAPHICS' AND outcome = 'DENIED'") == 2);
    CHECK(f.rowText("SELECT full_name FROM patient WHERE uhid = 1") == "Ravi Kumar");

    auto missing = f.service->updateDemographics(receptionist(), 999, d);
    REQUIRE_FALSE(missing.ok());
    CHECK(missing.error().kind == ErrorKind::NotFound);
}

TEST_CASE("TC-Reg-03 a store failure while saving leaves the old details") {
    UpdateFixture f;
    const auto uhid = f.addPatient("Ravi Kumar");
    f.db().setFaultHook([](const char* point) {
        if (std::string(point) == "patient_update_before_commit") throw InjectedFault(point);
    });
    auto r = f.service->updateDemographics(receptionist(), uhid, draft("Ravi Changed", "05-03-1990", "M", std::string("9123456780")));
    f.db().setFaultHook(nullptr);
    REQUIRE_FALSE(r.ok());
    CHECK(r.error().kind == ErrorKind::StorageFailure);
    CHECK(f.rowText("SELECT full_name FROM patient WHERE uhid = 1") == "Ravi Kumar");
    CHECK(f.count("SELECT count(*) FROM audit_entry WHERE action = 'PATIENT_UPDATE_DEMOGRAPHICS'") == 0);
}

TEST_CASE("Console TC-Reg-03: the Receptionist corrects a patient through the menu") {
    UpdateFixture f;
    f.addPatient("Ravi Kumar", "05-03-1990", "M", std::string("9876543210"));

    const std::string out = runReception(f,
                                         "8\n"
                                         "HMS-000001\n"
                                         "Ravi K. Kumar\n"    // new name
                                         "31-02-1990\n"       // impossible date -> asked again
                                         "\n"                 // keep the date of birth
                                         "X\n"                // bad gender -> asked again
                                         "\n"                 // keep the gender
                                         "\n"                 // keep the address
                                         "12345\n"            // bad mobile -> asked again
                                         "98765 11111\n"      // new mobile
                                         "Y\n"                // save
                                         "0\n");
    CHECK(contains(out, "8. Correct a patient's details"));
    CHECK(contains(out, "not a real calendar date"));
    CHECK(contains(out, "Gender: must be M (Male), F (Female) or O (Other)"));
    CHECK(contains(out, "Mobile number: must be a 10-digit Indian mobile number"));
    CHECK(contains(out, "Name          : Ravi K. Kumar"));
    CHECK(contains(out, "Details saved for HMS-000001. Changed: full_name phone"));
    CHECK(f.rowText("SELECT full_name FROM patient WHERE uhid = 1") == "Ravi K. Kumar");
    CHECK(f.rowText("SELECT phone FROM patient WHERE uhid = 1") == "9876511111");
}

TEST_CASE("Console TC-Reg-03: declining the confirmation, an unknown UHID and keeping everything change nothing") {
    UpdateFixture f;
    f.addPatient("Ravi Kumar", "05-03-1990", "M", std::string("9876543210"));

    const std::string declined = runReception(f, "8\nHMS-000001\nSomeone New\n\n\n\n\nN\n0\n");
    CHECK(contains(declined, "Cancelled. Nothing was changed."));
    CHECK(f.rowText("SELECT full_name FROM patient WHERE uhid = 1") == "Ravi Kumar");

    const std::string unknown = runReception(f, "8\nHMS-000099\n0\n");
    CHECK(contains(unknown, "No patient found with UHID HMS-000099"));

    const std::string kept = runReception(f, "8\nHMS-000001\n\n\n\n\n\nY\n0\n");
    CHECK(contains(kept, "Nothing was changed: the details are the same as before."));
    CHECK(f.count("SELECT count(*) FROM audit_entry WHERE action = 'PATIENT_UPDATE_DEMOGRAPHICS'") == 0);
}
