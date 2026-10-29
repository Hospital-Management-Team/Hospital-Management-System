#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "consultation/consultation.hpp"
#include "data/database.hpp"

namespace hms {

// A draft that passed validation: numbers parsed, text trimmed.
struct ValidConsultation {
    std::int64_t appointmentId = 0;
    std::int64_t uhid = 0;
    std::int64_t doctorStaffId = 0;
    std::string diagnosis;
    struct Rx { std::string medicineName, dosage; std::int64_t quantity = 0; };
    struct Lab { std::string testName; std::optional<std::string> instructions; };
    std::vector<Rx> prescriptions;
    std::vector<Lab> labOrders;
};

class ConsultationRepository {
public:
    explicit ConsultationRepository(Database& db) : db_(db) {}

    bool patientExists(std::int64_t uhid);
    bool existsForAppointment(std::int64_t appointmentId);
    std::int64_t insert(const ValidConsultation& c, const std::string& recordedBy, const std::string& nowUtc);
    std::optional<ConsultationRecord> findById(std::int64_t id);
    // A patient's consultations, newest first.
    std::vector<ConsultationRecord> listForPatient(std::int64_t uhid);

private:
    void loadLines(ConsultationRecord& r);

    Database& db_;
};

}  // namespace hms
