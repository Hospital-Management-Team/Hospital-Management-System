#pragma once
// Laboratory types (SRS HMS-F-040; SAD 3.4 "Lab Module"; issue #8).

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "core/numbered_id.hpp"

namespace hms {

inline std::string formatLabOrderId(std::int64_t id) { return formatNumbered("LAB", id); }
inline std::optional<std::int64_t> parseLabOrderId(std::string_view text) { return parseNumbered(text, "LAB"); }

// One saved version of a result. Updating a result adds a revision; earlier
// revisions are kept and the store refuses to change or delete them.
struct LabResultRevision {
    int revision = 1;
    std::string text;
    std::string enteredBy;  // staff username
    std::string enteredAt;  // UTC, as stored
};

// A test the doctor ordered, with its patient and its latest result
// (AC-HMS-F-040: the result is linked to the correct order and is visible to
// the treating doctor).
struct LabOrderView {
    std::int64_t orderId = 0;
    std::int64_t consultationId = 0;
    std::int64_t uhid = 0;
    std::string patientName;
    std::string doctorUsername;
    std::string testName;
    std::optional<std::string> instructions;
    std::string orderedAt;  // UTC, as stored
    int revisionCount = 0;  // 0 = no result yet
    std::optional<LabResultRevision> latest;
    bool hasResult() const { return revisionCount > 0; }
};

}  // namespace hms
