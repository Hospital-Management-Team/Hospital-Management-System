#pragma once
// Field rules for registering or updating a patient (SRS HMS-F-001,
// AC-HMS-F-001). Each function validates one field so the console can
// re-prompt immediately; validatePatient() checks a whole draft and reports
// every invalid field at once ("Every invalid field is named in the error").

#include <optional>
#include <string>
#include <string_view>

#include "config/policy.hpp"
#include "core/result.hpp"
#include "patient/patient.hpp"

namespace hms::patient_rules {

inline constexpr const char* kFullName = "Full name";
inline constexpr const char* kDateOfBirth = "Date of birth";
inline constexpr const char* kAge = "Age";
inline constexpr const char* kGender = "Gender";
inline constexpr const char* kAddress = "Address";

// Returns the name trimmed with single spaces (letter case kept).
Result<std::string> validateFullName(std::string_view raw, const PatientPolicy& policy);

struct DobValue {
    Date date;
    bool estimated = false;
};

// Exactly one of dobText / ageText must be non-empty.
Result<DobValue> validateDobOrAge(std::string_view dobText, std::string_view ageText, const Date& today,
                                  const PatientPolicy& policy);

// Just the date-of-birth rule (used when the console asks for it first).
Result<Date> validateDateOfBirth(std::string_view raw, const Date& today, const PatientPolicy& policy);

// Just the age rule: returns the estimated date of birth.
Result<Date> validateAge(std::string_view raw, const Date& today, const PatientPolicy& policy);

Result<Gender> validateGender(std::string_view raw);

Result<std::string> validateAddress(std::string_view raw, const PatientPolicy& policy);

// Empty means "no phone recorded" (the phone is optional).
Result<std::optional<std::string>> validateOptionalPhone(std::string_view raw);

Result<NewPatient> validatePatient(const PatientDraft& draft, const Date& today, const PatientPolicy& policy);

}  // namespace hms::patient_rules
