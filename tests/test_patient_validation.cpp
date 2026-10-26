// TC-Reg-01: mandatory fields and formats (SRS v11 draft HMS-F-001).
#include <doctest.h>

#include <algorithm>

#include "patient/patient_validation.hpp"
#include "patient_test_support.hpp"

using namespace hms;
using namespace hms::patient_rules;

namespace {
const Date kToday{2026, 10, 25};
const PatientPolicy kPolicy{};

bool namesField(const Error& e, const std::string& field) {
    return std::any_of(e.fields.begin(), e.fields.end(), [&](const FieldError& f) { return f.field == field; });
}
}  // namespace

TEST_CASE("TC-Reg-01 valid draft is accepted and normalised") {
    auto d = test::draft("  Ravi   Kumar ", "05-03-1990", "m", "+91 98765 43210", "  12 MG Road  ");
    auto r = validatePatient(d, kToday, kPolicy);
    REQUIRE(r.ok());
    CHECK(r.value().fullName == "Ravi Kumar");
    CHECK(r.value().normalisedName == "ravi kumar");
    CHECK(r.value().dateOfBirth == Date{1990, 3, 5});
    CHECK_FALSE(r.value().dobEstimated);
    CHECK(r.value().gender == Gender::Male);
    CHECK(r.value().address == "12 MG Road");
    CHECK(r.value().phone.value() == "9876543210");
}

TEST_CASE("TC-Reg-01 phone is optional") {
    auto r = validatePatient(test::draft("Ravi Kumar", "05-03-1990", "M", ""), kToday, kPolicy);
    REQUIRE(r.ok());
    CHECK_FALSE(r.value().phone.has_value());
}

TEST_CASE("TC-Reg-01 every invalid field is named in one error") {
    PatientDraft d;  // everything empty
    auto r = validatePatient(d, kToday, kPolicy);
    REQUIRE_FALSE(r.ok());
    CHECK(r.error().kind == ErrorKind::Validation);
    CHECK(namesField(r.error(), kFullName));
    CHECK(namesField(r.error(), kDateOfBirth));
    CHECK(namesField(r.error(), kGender));
    CHECK(namesField(r.error(), kAddress));
    CHECK_FALSE(namesField(r.error(), "Mobile number"));  // empty phone is allowed

    auto bad = validatePatient(test::draft("Ravi", "05-03-1990", "M", "12345"), kToday, kPolicy);
    REQUIRE_FALSE(bad.ok());
    CHECK(namesField(bad.error(), "Mobile number"));
}

TEST_CASE("TC-Reg-01 name length 1-100 characters, must contain a letter") {
    CHECK(validateFullName(std::string(100, 'a'), kPolicy).ok());
    CHECK_FALSE(validateFullName(std::string(101, 'a'), kPolicy).ok());
    CHECK_FALSE(validateFullName("", kPolicy).ok());
    CHECK_FALSE(validateFullName("12345", kPolicy).ok());
    CHECK(validateFullName("K. S. Rao", kPolicy).ok());
    CHECK(validateFullName(u8"रवि कुमार", kPolicy).ok());  // Devanagari
    // 100 Devanagari characters are 300 bytes but still 100 characters.
    std::string hundred;
    for (int i = 0; i < 100; ++i) hundred += u8"र";
    CHECK(validateFullName(hundred, kPolicy).ok());
}

TEST_CASE("TC-Reg-01 address 1-200 characters") {
    CHECK(validateAddress(std::string(200, 'x'), kPolicy).ok());
    CHECK_FALSE(validateAddress(std::string(201, 'x'), kPolicy).ok());
    CHECK_FALSE(validateAddress("   ", kPolicy).ok());
}

TEST_CASE("TC-Reg-01 date of birth: not in the future, not before 01-01-1900") {
    CHECK(validateDateOfBirth("25-10-2026", kToday, kPolicy).ok());  // today
    CHECK_FALSE(validateDateOfBirth("26-10-2026", kToday, kPolicy).ok());
    CHECK(validateDateOfBirth("01-01-1900", kToday, kPolicy).ok());
    CHECK_FALSE(validateDateOfBirth("31-12-1899", kToday, kPolicy).ok());
    CHECK_FALSE(validateDateOfBirth("31-02-2000", kToday, kPolicy).ok());
}

TEST_CASE("TC-Reg-01 (D-03) age instead of date of birth gives an estimated date of birth") {
    auto r = validateDobOrAge("", "40", kToday, kPolicy);
    REQUIRE(r.ok());
    CHECK(r.value().date == Date{1986, 1, 1});
    CHECK(r.value().estimated);
    CHECK(ageInYears(r.value().date, kToday) == 40);  // derived age equals entered age

    CHECK(validateDobOrAge("", "0", kToday, kPolicy).ok());           // newborn
    CHECK_FALSE(validateDobOrAge("", "126", kToday, kPolicy).ok());   // above the limit
    CHECK_FALSE(validateDobOrAge("", "forty", kToday, kPolicy).ok());
    CHECK_FALSE(validateDobOrAge("05-03-1990", "36", kToday, kPolicy).ok());  // both given
    CHECK_FALSE(validateDobOrAge("", "", kToday, kPolicy).ok());              // neither given
}

TEST_CASE("TC-Reg-01 gender: Male / Female / Other") {
    CHECK(validateGender("M").value() == Gender::Male);
    CHECK(validateGender("female").value() == Gender::Female);
    CHECK(validateGender(" o ").value() == Gender::Other);
    CHECK_FALSE(validateGender("x").ok());
    CHECK_FALSE(validateGender("").ok());
}
