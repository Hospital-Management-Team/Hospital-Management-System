#include "patient/patient.hpp"

namespace hms {

std::string to_string(Gender g) {
    switch (g) {
        case Gender::Male: return "Male";
        case Gender::Female: return "Female";
        case Gender::Other: return "Other";
    }
    return "Other";
}

std::optional<Gender> genderFromStorage(const std::string& s) {
    if (s == "Male") return Gender::Male;
    if (s == "Female") return Gender::Female;
    if (s == "Other") return Gender::Other;
    return std::nullopt;
}

std::string reasonCode(DuplicateReason r) {
    switch (r) {
        case DuplicateReason::TwinsOrMultipleBirth: return "TWINS_OR_MULTIPLE_BIRTH";
        case DuplicateReason::DifferentPersonSameNameAndDob: return "DIFFERENT_PERSON_SAME_NAME_DOB";
        case DuplicateReason::Other: return "OTHER";
    }
    return "OTHER";
}

std::string reasonLabel(DuplicateReason r) {
    switch (r) {
        case DuplicateReason::TwinsOrMultipleBirth: return "Twins or multiple birth";
        case DuplicateReason::DifferentPersonSameNameAndDob:
            return "Different person with the same name and date of birth";
        case DuplicateReason::Other: return "Other";
    }
    return "Other";
}

std::string to_string(CandidateKind k) { return k == CandidateKind::Strong ? "STRONG" : "SAME_PHONE"; }

}  // namespace hms
