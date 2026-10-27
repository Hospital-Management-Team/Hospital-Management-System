#include "patient/patient_validation.hpp"

#include "core/text.hpp"
#include "patient/phone.hpp"
#include "validation/input.hpp"

namespace hms::patient_rules {

namespace {
// A name must contain at least one letter: an ASCII letter or any non-ASCII
// character (letters of Indian and other scripts). Rejects "123" or "--".
bool hasLetter(std::string_view s) {
    for (char c : s) {
        const auto u = static_cast<unsigned char>(c);
        if ((u >= 'A' && u <= 'Z') || (u >= 'a' && u <= 'z') || u >= 0x80) return true;
    }
    return false;
}
}  // namespace

Result<std::string> validateFullName(std::string_view raw, const PatientPolicy& policy) {
    auto line = input::requireLine(raw, kFullName);
    if (!line) return line;
    std::string name = text::collapseSpaces(line.value());
    const auto len = text::utf8Length(name);
    if (len > policy.maxNameChars) {
        return Error::validation(kFullName, "must be at most " + std::to_string(policy.maxNameChars) +
                                                " characters (it has " + std::to_string(len) + ")");
    }
    if (!hasLetter(name)) return Error::validation(kFullName, "must contain at least one letter");
    return name;
}

Result<Date> validateDateOfBirth(std::string_view raw, const Date& today, const PatientPolicy& policy) {
    auto line = input::requireLine(raw, kDateOfBirth);
    if (!line) return line.error();
    auto date = input::parseDate(line.value(), kDateOfBirth);
    if (!date) return date;
    if (date.value() > today) return Error::validation(kDateOfBirth, "cannot be in the future");
    if (date.value() < policy.earliestDateOfBirth) {
        return Error::validation(kDateOfBirth, "cannot be before " + policy.earliestDateOfBirth.toDisplay());
    }
    return date;
}

Result<Date> validateAge(std::string_view raw, const Date& today, const PatientPolicy& policy) {
    auto line = input::requireLine(raw, kAge);
    if (!line) return line.error();
    auto years = input::parseWholeNumber(line.value(), kAge, 0, policy.maxAgeYears);
    if (!years) return years.error();
    // Estimated date of birth: 1 January of the birth year. The derived age
    // then equals the entered age for the whole of the current year.
    const Date estimated{today.year - static_cast<int>(years.value()), 1, 1};
    if (estimated < policy.earliestDateOfBirth) {
        return Error::validation(kAge, "gives a birth year before " +
                                           std::to_string(policy.earliestDateOfBirth.year));
    }
    return estimated;
}

Result<DobValue> validateDobOrAge(std::string_view dobText, std::string_view ageText, const Date& today,
                                  const PatientPolicy& policy) {
    auto dobLine = input::cleanLine(dobText, kDateOfBirth);
    if (!dobLine) return dobLine.error();
    auto ageLine = input::cleanLine(ageText, kAge);
    if (!ageLine) return ageLine.error();

    const bool hasDob = !dobLine.value().empty();
    const bool hasAge = !ageLine.value().empty();
    if (hasDob && hasAge) {
        return Error::validation(kDateOfBirth, "enter either the date of birth or the age, not both");
    }
    if (!hasDob && !hasAge) {
        return Error::validation(kDateOfBirth, "is required (if it is not known, enter the age in years instead)");
    }
    if (hasDob) {
        auto d = validateDateOfBirth(dobLine.value(), today, policy);
        if (!d) return d.error();
        return DobValue{d.value(), false};
    }
    auto a = validateAge(ageLine.value(), today, policy);
    if (!a) return a.error();
    return DobValue{a.value(), true};
}

Result<Gender> validateGender(std::string_view raw) {
    auto line = input::requireLine(raw, kGender);
    if (!line) return line.error();
    const std::string v = text::asciiLower(line.value());
    if (v == "m" || v == "male") return Gender::Male;
    if (v == "f" || v == "female") return Gender::Female;
    if (v == "o" || v == "other") return Gender::Other;
    return Error::validation(kGender, "must be M (Male), F (Female) or O (Other)");
}

Result<std::string> validateAddress(std::string_view raw, const PatientPolicy& policy) {
    auto line = input::requireLine(raw, kAddress);
    if (!line) return line;
    const auto len = text::utf8Length(line.value());
    if (len > policy.maxAddressChars) {
        return Error::validation(kAddress, "must be at most " + std::to_string(policy.maxAddressChars) +
                                               " characters (it has " + std::to_string(len) + ")");
    }
    return line;
}

Result<std::string> validatePhone(std::string_view raw) {
    auto line = input::requireLine(raw, phone::kField);
    if (!line) return line;
    return phone::canonicalise(line.value());
}

Result<NewPatient> validatePatient(const PatientDraft& draft, const Date& today, const PatientPolicy& policy) {
    std::vector<FieldError> errors;
    auto collect = [&errors](const Error& e) {
        for (const auto& f : e.fields) errors.push_back(f);
    };

    NewPatient p;
    if (auto r = validateFullName(draft.fullName, policy)) {
        p.fullName = r.value();
        p.normalisedName = text::normaliseName(p.fullName);
    } else {
        collect(r.error());
    }
    if (auto r = validateDobOrAge(draft.dateOfBirth, draft.ageYears, today, policy)) {
        p.dateOfBirth = r.value().date;
        p.dobEstimated = r.value().estimated;
    } else {
        collect(r.error());
    }
    if (auto r = validateGender(draft.gender)) {
        p.gender = r.value();
    } else {
        collect(r.error());
    }
    if (auto r = validateAddress(draft.address, policy)) {
        p.address = r.value();
    } else {
        collect(r.error());
    }
    if (draft.noPhone) {
        // "No phone available" was confirmed: a number must not be given as well.
        auto line = input::cleanLine(draft.phone, phone::kField);
        if (!line) {
            collect(line.error());
        } else if (!line.value().empty()) {
            collect(Error::validation(phone::kField, "must be empty when \"no phone available\" is chosen"));
        } else {
            p.noPhoneConfirmed = true;
        }
    } else if (auto r = validatePhone(draft.phone)) {
        p.phone = r.value();
    } else {
        collect(r.error());
    }

    if (!errors.empty()) return Error::validation(std::move(errors));
    return p;
}

}  // namespace hms::patient_rules
