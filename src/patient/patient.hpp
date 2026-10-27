#pragma once
// Patient Management Module - types (SRS HMS-F-001..006; SAD 4.4.1).

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "core/clock.hpp"

namespace hms {

enum class Gender { Male, Female, Other };
std::string to_string(Gender g);
std::optional<Gender> genderFromStorage(const std::string& s);

// Duplicate reason codes (SRS section 1.4) - a fixed list, no free text.
enum class DuplicateReason { TwinsOrMultipleBirth, DifferentPersonSameNameAndDob, Other };
std::string reasonCode(DuplicateReason r);   // stored and audited
std::string reasonLabel(DuplicateReason r);  // shown to staff

// What reception typed, field by field, before validation. The service
// validates it again even if the console already did (defence in depth, and
// the service is what the tests and future GUIs call).
struct PatientDraft {
    std::string fullName;
    std::string dateOfBirth;  // DD-MM-YYYY, or empty when the age is given
    std::string ageYears;     // whole years, or empty when the date of birth is given
    std::string gender;       // M / F / O or Male / Female / Other
    std::string address;
    std::string phone;        // required, unless noPhone is set
    bool noPhone = false;     // staff confirmed: no phone for the patient or anyone with them
};

// A draft that passed validation, in stored form.
struct NewPatient {
    std::string fullName;        // trimmed, single spaces, original letter case
    std::string normalisedName;  // as above, case-folded (duplicate check, search)
    Date dateOfBirth;
    bool dobEstimated = false;   // true when entered as an age
    Gender gender = Gender::Other;
    std::string address;
    std::optional<std::string> phone;  // canonical 10 digits; empty only when noPhoneConfirmed
    bool noPhoneConfirmed = false;
};

struct PatientRecord {
    std::int64_t uhid = 0;
    std::string fullName;
    Date dateOfBirth;
    bool dobEstimated = false;
    Gender gender = Gender::Other;
    std::string address;
    // Empty with noPhoneConfirmed: staff confirmed "no phone available".
    // Empty without it: a record saved before the number became required.
    std::optional<std::string> phone;
    bool noPhoneConfirmed = false;
};

enum class CandidateKind { Strong, SamePhone };
std::string to_string(CandidateKind k);  // "STRONG" / "SAME_PHONE"

struct Candidate {
    PatientRecord patient;
    CandidateKind kind = CandidateKind::Strong;
    bool newSinceReview = false;  // HMS-F-002(9): appeared after the screen was shown
};

// Staff decision for one review screen (HMS-F-002(9)).
struct DuplicateDecision {
    enum class Kind { None, DifferentPerson };
    Kind kind = Kind::None;
    std::optional<DuplicateReason> reason;      // required iff a strong candidate is listed
    std::vector<std::int64_t> reviewedUhids;    // the candidates that were shown

    static DuplicateDecision none() { return {}; }
};

struct RegistrationOutcome {
    enum class Status { Registered, NeedsDecision };
    Status status = Status::Registered;
    std::int64_t uhid = 0;                 // set when Registered
    std::vector<Candidate> candidates;     // set when NeedsDecision
    bool reasonRequired = false;           // a strong candidate is listed
    bool differentPersonAllowed = true;    // false under the v1.1 phone-blocks policy
};

enum class SearchMode { Uhid, Name, Phone };

// Returning-patient lookup by name and date of birth (HMS-F-004).
// A record whose date of birth was estimated from an age also matches when its
// year is within one year of the date given (the estimate is 1 January).
struct NameDobQuery {
    std::string name;         // any part of the name
    std::string dateOfBirth;  // DD-MM-YYYY
};

struct SearchResult {
    std::vector<PatientRecord> rows;
    std::size_t totalMatches = 0;
    bool truncated() const { return totalMatches > rows.size(); }
};

}  // namespace hms
