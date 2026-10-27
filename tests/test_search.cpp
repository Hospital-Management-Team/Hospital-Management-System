// TC-Rec-02: patient search (SRS v11 draft HMS-F-004). The 100,000-patient
// timing belongs to TC-Perf-01 (see tools/hms_seed --bench).
#include <doctest.h>

#include "patient/uhid.hpp"
#include "patient_test_support.hpp"

using namespace hms;
using hms::test::StoreFixture;

namespace {
std::int64_t add(StoreFixture& f, const PatientDraft& d) {
    auto r = f.service->registerPatient(test::receptionist(), d, DuplicateDecision::none());
    REQUIRE(r.ok());
    if (r.value().status == RegistrationOutcome::Status::Registered) return r.value().uhid;
    DuplicateDecision dec;
    dec.kind = DuplicateDecision::Kind::DifferentPerson;
    dec.reason = DuplicateReason::Other;
    for (const auto& c : r.value().candidates) dec.reviewedUhids.push_back(c.patient.uhid);
    auto again = f.service->registerPatient(test::receptionist(), d, dec);
    REQUIRE(again.ok());
    REQUIRE(again.value().status == RegistrationOutcome::Status::Registered);
    return again.value().uhid;
}
}  // namespace

TEST_CASE("TC-Rec-02 search by UHID is an exact match in any accepted format") {
    StoreFixture f;
    const auto id = add(f, test::draft("Ravi Kumar", "05-03-1990", "M"));
    add(f, test::draft("Ravindra Rao", "01-01-1980", "M"));
    for (const std::string& q : {formatUhid(id), std::to_string(id), "hms-" + std::to_string(id)}) {
        CAPTURE(q);
        auto r = f.service->searchPatients(test::receptionist(), SearchMode::Uhid, q);
        REQUIRE(r.ok());
        REQUIRE(r.value().rows.size() == 1);
        CHECK(r.value().rows[0].uhid == id);
    }
    auto none = f.service->searchPatients(test::receptionist(), SearchMode::Uhid, "HMS-999999");
    REQUIRE(none.ok());
    CHECK(none.value().rows.empty());  // "No patient found" is shown by the UI
    CHECK_FALSE(f.service->searchPatients(test::receptionist(), SearchMode::Uhid, "abc").ok());
}

TEST_CASE("TC-Rec-02 name search is a case-insensitive substring match on the normalised name") {
    StoreFixture f;
    add(f, test::draft("Ravi Kumar", "05-03-1990", "M"));
    add(f, test::draft("Ravindra   RAO", "01-01-1980", "M"));
    add(f, test::draft("Anita Kumari", "11-11-1985", "F"));

    auto r = f.service->searchPatients(test::receptionist(), SearchMode::Name, "RAVI");
    REQUIRE(r.ok());
    CHECK(r.value().totalMatches == 2);
    auto k = f.service->searchPatients(test::receptionist(), SearchMode::Name, "  kumar ");
    CHECK(k.value().totalMatches == 2);  // Kumar and Kumari
    auto spaced = f.service->searchPatients(test::receptionist(), SearchMode::Name, "ravindra rao");
    CHECK(spaced.value().totalMatches == 1);  // repeated spaces collapsed in both
    auto none = f.service->searchPatients(test::receptionist(), SearchMode::Name, "zzz");
    CHECK(none.value().rows.empty());
    CHECK_FALSE(f.service->searchPatients(test::receptionist(), SearchMode::Name, "   ").ok());
}

TEST_CASE("TC-Rec-02 phone search normalises the entry and returns every patient sharing the number") {
    StoreFixture f;
    add(f, test::draft("Lakshmi Devi", "01-06-1975", "F", "9876543210"));
    add(f, test::draft("Arjun", "14-08-2015", "M", "+91 98765 43210"));
    add(f, test::draft("Someone Else", "01-01-1999", "M", "9000000000"));

    for (const char* q : {"9876543210", "098765-43210", "+91 98765 43210", "91 9876543210"}) {
        CAPTURE(q);
        auto r = f.service->searchPatients(test::receptionist(), SearchMode::Phone, q);
        REQUIRE(r.ok());
        CHECK(r.value().rows.size() == 2);
    }
    // An invalid or partial number is rejected with a message, never matched as partial digits.
    auto partial = f.service->searchPatients(test::receptionist(), SearchMode::Phone, "98765");
    REQUIRE_FALSE(partial.ok());
    CHECK(partial.error().fields.at(0).field == "Mobile number");
}

TEST_CASE("TC-Rec-02 names with quotes, SQL keywords and formula characters are stored and found as data") {
    StoreFixture f;
    const auto a = add(f, test::draft("D'Souza; DROP TABLE patient;--", "01-01-1990", "M"));
    const auto b = add(f, test::draft("=HYPERLINK(\"x\") Rao", "01-01-1991", "F"));
    const auto c = add(f, test::draft("100% Ravi_Kumar", "01-01-1992", "M"));
    CHECK(f.count("SELECT count(*) FROM patient") == 3);

    auto r1 = f.service->searchPatients(test::receptionist(), SearchMode::Name, "d'souza; drop");
    REQUIRE(r1.value().rows.size() == 1);
    CHECK(r1.value().rows[0].uhid == a);
    CHECK(r1.value().rows[0].fullName == "D'Souza; DROP TABLE patient;--");
    auto r2 = f.service->searchPatients(test::receptionist(), SearchMode::Name, "=hyperlink");
    REQUIRE(r2.value().rows.size() == 1);
    CHECK(r2.value().rows[0].uhid == b);
    // % and _ are not wildcards (search uses instr, not LIKE).
    auto r3 = f.service->searchPatients(test::receptionist(), SearchMode::Name, "%");
    REQUIRE(r3.value().rows.size() == 1);
    CHECK(r3.value().rows[0].uhid == c);
    auto r4 = f.service->searchPatients(test::receptionist(), SearchMode::Name, "i_k");
    CHECK(r4.value().rows.size() == 1);
}

TEST_CASE("TC-Rec-02 results are demographic only and capped with the true total reported") {
    AppPolicy p;
    p.patient.maxSearchResults = 3;
    StoreFixture f(p);
    for (int i = 0; i < 5; ++i) add(f, test::draft("Priya Sharma " + std::to_string(i), "01-01-199" + std::to_string(i), "F"));
    auto r = f.service->searchPatients(test::receptionist(), SearchMode::Name, "priya");
    REQUIRE(r.ok());
    CHECK(r.value().rows.size() == 3);
    CHECK(r.value().totalMatches == 5);
    CHECK(r.value().truncated());
}

TEST_CASE("SR-001 search: Receptionist and Doctor allowed, other roles denied and audited") {
    StoreFixture f;
    add(f, test::draft("Ravi Kumar", "05-03-1990", "M"));
    CHECK(f.service->searchPatients(test::doctor(), SearchMode::Name, "ravi").ok());
    auto denied = f.service->searchPatients(test::pharmacist(), SearchMode::Name, "ravi");
    REQUIRE_FALSE(denied.ok());
    CHECK(denied.error().kind == ErrorKind::AccessDenied);
    CHECK(f.count("SELECT count(*) FROM audit_entry WHERE outcome = 'DENIED' AND user_id = 'STAFF-P1'") == 1);
    // Ordinary searches are not audited (SRS HMS-SR-004 decision).
    CHECK(f.count("SELECT count(*) FROM audit_entry WHERE action = 'PATIENT_SEARCH' AND outcome = 'OK'") == 0);
}

TEST_CASE("getPatient returns the record or NotFound") {
    StoreFixture f;
    const auto id = add(f, test::draft("Ravi Kumar", "05-03-1990", "M", "9876543210"));
    auto r = f.service->getPatient(test::doctor(), id);
    REQUIRE(r.ok());
    CHECK(r.value().fullName == "Ravi Kumar");
    CHECK(r.value().phone.value() == "9876543210");
    auto missing = f.service->getPatient(test::receptionist(), 999);
    REQUIRE_FALSE(missing.ok());
    CHECK(missing.error().kind == ErrorKind::NotFound);
}

// ------------------------------------------- name + date of birth (returning patient)

namespace {
NameDobQuery q(std::string name, std::string dob) { return NameDobQuery{std::move(name), std::move(dob)}; }
bool namesField(const Error& e, const std::string& field) {
    for (const auto& fe : e.fields)
        if (fe.field == field) return true;
    return false;
}
}  // namespace

TEST_CASE("TC-Rec-02 name and date of birth: both must match; exact dates of birth first") {
    StoreFixture f;
    const auto kamala = add(f, test::draftNoPhone("Kamala Bai", "03-02-1950", "F"));
    add(f, test::draft("Kamala Rao", "03-02-1951", "F"));    // name matches, date does not
    add(f, test::draft("Shanti Bai", "03-02-1950", "F"));    // date matches, name does not
    auto r = f.service->searchByNameAndDob(test::receptionist(), q("KAMALA", "3-2-1950"));
    REQUIRE(r.ok());
    REQUIRE(r.value().rows.size() == 1);
    CHECK(r.value().rows[0].uhid == kamala);
    CHECK(r.value().rows[0].noPhoneConfirmed);
}

TEST_CASE("TC-Rec-02 name and date of birth: a date estimated from an age matches within one year") {
    StoreFixture f;
    // Registered by age: stored as 01-01 of the birth year, flagged as estimated.
    PatientDraft byAge = test::draft("Geeta Bai", "", "F");
    byAge.ageYears = "70";  // FixedClock year 2026 -> 1956-01-01
    const auto geeta = add(f, byAge);
    add(f, test::draft("Geeta Bai", "20-07-1940", "F"));  // exact, but a different year
    for (const char* dob : {"15-08-1955", "15-08-1956", "15-08-1957"}) {
        CAPTURE(dob);
        auto r = f.service->searchByNameAndDob(test::receptionist(), q("geeta", dob));
        REQUIRE(r.ok());
        REQUIRE(r.value().rows.size() == 1);
        CHECK(r.value().rows[0].uhid == geeta);
    }
    auto far = f.service->searchByNameAndDob(test::receptionist(), q("geeta", "15-08-1960"));
    REQUIRE(far.ok());
    CHECK(far.value().rows.empty());
}

TEST_CASE("TC-Rec-02 name and date of birth: every invalid field is named; other roles denied") {
    StoreFixture f;
    auto bad = f.service->searchByNameAndDob(test::receptionist(), q("", "31-02-1990"));
    REQUIRE_FALSE(bad.ok());
    CHECK(bad.error().kind == ErrorKind::Validation);
    CHECK(namesField(bad.error(), "Name"));
    CHECK(namesField(bad.error(), "Date of birth"));

    CHECK(f.service->searchByNameAndDob(test::doctor(), q("a", "01-01-1990")).ok());
    auto denied = f.service->searchByNameAndDob(test::pharmacist(), q("a", "01-01-1990"));
    REQUIRE_FALSE(denied.ok());
    CHECK(denied.error().kind == ErrorKind::AccessDenied);
    CHECK(f.count("SELECT count(*) FROM audit_entry WHERE outcome = 'DENIED'") == 1);
}
