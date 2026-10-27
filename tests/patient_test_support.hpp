#pragma once
// Fixtures for the patient tests (issue #2): an opened store with a
// PatientService, the sessions used by the tests, and a draft builder.

#include <memory>
#include <optional>
#include <string>

#include "config/policy.hpp"
#include "patient/patient_service.hpp"
#include "test_support.hpp"

namespace hms::test {

inline Session receptionist() { return Session{"STAFF-R1", "Test receptionist", Role::Receptionist, false}; }
inline Session doctor() { return Session{"STAFF-D1", "Test doctor", Role::Doctor, false}; }
inline Session pharmacist() { return Session{"STAFF-P1", "Test pharmacist", Role::Pharmacist, false}; }

struct StoreFixture : TempStore {
    AppPolicy policy;
    std::unique_ptr<PatientService> service;

    explicit StoreFixture(AppPolicy p = AppPolicy{}) : policy(p) {
        service = std::make_unique<PatientService>(db(), clock, policy);
    }
    ~StoreFixture() override { service.reset(); }
};

// Every patient needs a mobile number (or a confirmed "no phone available").
// When a test does not care which number, each call gets a different valid one,
// so no accidental "same phone" review appears.
inline std::string nextPhone() {
    static long long n = 0;
    return "9" + std::to_string(100000000 + ++n);
}

inline PatientDraft draft(std::string name, std::string dob, std::string gender,
                          std::optional<std::string> phoneNo = std::nullopt,
                          std::string address = "12 MG Road, Bengaluru") {
    PatientDraft d;
    d.fullName = std::move(name);
    d.dateOfBirth = std::move(dob);
    d.gender = std::move(gender);
    d.address = std::move(address);
    d.phone = phoneNo ? std::move(*phoneNo) : nextPhone();
    return d;
}

// A draft for a patient with "no phone available" confirmed.
inline PatientDraft draftNoPhone(std::string name, std::string dob, std::string gender) {
    PatientDraft d = draft(std::move(name), std::move(dob), std::move(gender), std::string());
    d.noPhone = true;
    return d;
}

}  // namespace hms::test
