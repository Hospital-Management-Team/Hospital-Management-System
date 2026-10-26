#include "patient/phone.hpp"

namespace hms::phone {

namespace {
const char* kRule =
    "must be a 10-digit Indian mobile number starting with 6, 7, 8 or 9 "
    "(0, 91 or +91 in front is allowed), for example 98765 43210";
}

Result<std::string> canonicalise(std::string_view raw) {
    std::string digits;
    bool plus = false;
    bool seenSignificant = false;
    for (char c : raw) {
        if (c == ' ' || c == '-' || c == '.' || c == '(' || c == ')') continue;
        if (c == '+') {
            if (seenSignificant || plus) return Error::validation(kField, kRule);
            plus = true;
            seenSignificant = true;
            continue;
        }
        if (c < '0' || c > '9') return Error::validation(kField, kRule);
        digits.push_back(c);
        seenSignificant = true;
    }
    if (digits.empty()) return Error::validation(kField, kRule);

    std::string national;
    if (plus) {
        // "+" is allowed only in front of the country code 91.
        if (digits.size() == 12 && digits.compare(0, 2, "91") == 0) national = digits.substr(2);
    } else if (digits.size() == 10) {
        national = digits;
    } else if (digits.size() == 11 && digits[0] == '0') {
        national = digits.substr(1);
    } else if (digits.size() == 12 && digits.compare(0, 2, "91") == 0) {
        national = digits.substr(2);
    }
    if (national.size() != 10 || national[0] < '6') return Error::validation(kField, kRule);
    return national;
}

std::string display(const std::string& canonical) {
    if (canonical.size() != 10) return canonical;
    return "+91 " + canonical.substr(0, 5) + " " + canonical.substr(5);
}

}  // namespace hms::phone
