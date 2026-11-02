#pragma once
// Pharmacy types (SRS HMS-F-041; SAD 3.4 "Pharmacy Module"; issue #8).

#include <cstdint>
#include <optional>
#include <string>

#include "core/numbered_id.hpp"

namespace hms {

inline std::string formatMedicineId(std::int64_t id) { return formatNumbered("MED", id); }
inline std::optional<std::int64_t> parseMedicineId(std::string_view text) { return parseNumbered(text, "MED"); }
// A prescription line the doctor wrote on a consultation (issue #5).
inline std::string formatPrescriptionId(std::int64_t id) { return formatNumbered("RX", id); }
inline std::optional<std::int64_t> parsePrescriptionId(std::string_view text) { return parseNumbered(text, "RX"); }

struct Medicine {
    std::int64_t medicineId = 0;
    std::string name;
    std::string unit;  // "tablet", "ml", ...
    std::int64_t stockQuantity = 0;
    bool active = true;     // an unavailable medicine cannot be dispensed or restocked
    bool lowStock = false;  // active and the stock is below the configured threshold (HMS-F-041)
};

// A medicine as typed by the pharmacist.
struct MedicineDraft {
    std::string name;
    std::string unit;
    std::string initialStock;  // whole units; blank means 0
};

// The name and unit of a medicine as typed when it is corrected.
struct MedicineEdit {
    std::string name;
    std::string unit;
};

// A prescription line with how much of it has been dispensed so far.
struct DispenseLine {
    std::int64_t itemId = 0;
    std::int64_t consultationId = 0;
    std::int64_t uhid = 0;
    std::string patientName;
    std::string doctorUsername;
    std::string medicineName;  // as the doctor wrote it (free text, D-43)
    std::string dosage;
    std::int64_t prescribed = 0;
    std::int64_t dispensed = 0;
    std::string prescribedAt;  // UTC, as stored
    std::int64_t remaining() const { return prescribed - dispensed; }
};

struct DispenseOutcome {
    std::int64_t dispenseId = 0;
    std::int64_t itemId = 0;
    std::int64_t medicineId = 0;
    std::string medicineName;
    std::string unit;
    std::int64_t quantity = 0;
    std::int64_t remainingToDispense = 0;
    std::int64_t stockAfter = 0;
    bool lowStock = false;  // the low-stock alert fired: stock is now below the threshold
    std::int64_t threshold = 0;
};

}  // namespace hms
