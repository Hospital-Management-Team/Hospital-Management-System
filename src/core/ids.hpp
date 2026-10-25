#pragma once
// Identifier helpers.
//  - Event IDs: random 128-bit values written as 32 hex digits (SRS HMS-SR-004),
//    used by the audit trail.

#include <string>

namespace hms {

std::string newEventId();

}  // namespace hms
