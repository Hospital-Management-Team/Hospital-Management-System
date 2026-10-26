#pragma once
// UHID (Unique Health/Hospital ID): an integer from a store counter that is
// never reused (SAD data dictionary, Patient.uhid). Shown to staff as "HMS-"
// plus at least 6 digits.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace hms {

std::string formatUhid(std::int64_t uhid);

// Accepts "HMS-000123", "hms-123", "HMS123" and "123". Returns nullopt for
// anything else (including 0 and negative numbers).
std::optional<std::int64_t> parseUhid(std::string_view text);

}  // namespace hms
