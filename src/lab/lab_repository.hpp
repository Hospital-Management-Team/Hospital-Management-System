#pragma once
// Laboratory persistence (Data Access Module, lab part). SQL lives here only.

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "data/database.hpp"
#include "lab/lab.hpp"

namespace hms {

class LabRepository {
public:
    explicit LabRepository(Database& db) : db_(db) {}

    std::optional<LabOrderView> findOrder(std::int64_t orderId);
    // Orders with no result yet, oldest first.
    std::vector<LabOrderView> listPending(std::size_t limit);
    std::vector<LabOrderView> listForPatient(std::int64_t uhid);
    // Orders on consultations recorded by one doctor, for one patient.
    std::vector<LabOrderView> listForPatientByDoctor(std::int64_t uhid, std::int64_t doctorStaffId);
    bool patientExists(std::int64_t uhid);

    std::vector<LabResultRevision> history(std::int64_t orderId);
    std::optional<LabResultRevision> latest(std::int64_t orderId);
    void insertRevision(std::int64_t orderId, int revision, const std::string& text, const std::string& enteredBy,
                        const std::string& nowUtc);

private:
    std::vector<LabOrderView> query(const std::string& where, const std::string& tail,
                                    const std::function<void(Statement&)>& bind);

    Database& db_;
};

}  // namespace hms
