#pragma once
// Pharmacy persistence (Data Access Module, pharmacy part). SQL lives here only.

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "data/database.hpp"
#include "pharmacy/pharmacy.hpp"

namespace hms {

class PharmacyRepository {
public:
    explicit PharmacyRepository(Database& db) : db_(db) {}

    std::vector<Medicine> listMedicines();  // by name; lowStock is not set here
    std::optional<Medicine> findMedicine(std::int64_t medicineId);
    bool medicineNameExists(const std::string& name, std::optional<std::int64_t> exceptId = std::nullopt);
    void updateMedicine(std::int64_t medicineId, const std::string& name, const std::string& unit);
    void setActive(std::int64_t medicineId, bool active);
    std::int64_t insertMedicine(const std::string& name, const std::string& unit, const std::string& nowUtc);
    // Changes the stock and writes one stock-movement row (INITIAL, RESTOCK, DISPENSE or CORRECTION).
    // `note` is the reason of a CORRECTION and is empty for the others.
    void moveStock(std::int64_t medicineId, std::int64_t delta, const char* reason,
                   std::optional<std::int64_t> dispenseId, const std::string& recordedBy, const std::string& nowUtc,
                   const std::optional<std::string>& note = std::nullopt);
    std::int64_t movementTotal(std::int64_t medicineId);

    std::optional<DispenseLine> findLine(std::int64_t itemId);
    // Lines with something left to dispense, oldest first.
    std::vector<DispenseLine> listPending(std::size_t limit);
    std::vector<DispenseLine> listForPatient(std::int64_t uhid);
    bool patientExists(std::int64_t uhid);

    std::int64_t insertDispense(std::int64_t itemId, std::int64_t medicineId, std::int64_t quantity,
                                const std::string& dispensedBy, const std::string& nowUtc);

private:
    std::vector<DispenseLine> query(const std::string& where, const std::string& tail,
                                        const std::function<void(Statement&)>& bind);

    Database& db_;
};

}  // namespace hms
