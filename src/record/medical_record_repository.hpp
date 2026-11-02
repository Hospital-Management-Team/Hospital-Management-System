#pragma once
// Medical Records Module - storage (SAD 3.4, Data Access). Runs statements only;
// every business rule is in MedicalRecordService. Callers own the transaction.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "data/database.hpp"
#include "record/medical_record.hpp"

namespace hms {

class MedicalRecordRepository {
public:
    explicit MedicalRecordRepository(Database& db) : db_(db) {}

    std::optional<std::string> patientName(std::int64_t uhid);
    std::int64_t insert(std::int64_t uhid, const MedicalHistoryDraft& valid, std::int64_t authorStaffId,
                        const std::string& nowUtc);
    // A patient's versions, newest first.
    std::vector<MedicalHistoryVersion> listForPatient(std::int64_t uhid);

private:
    Database& db_;
};

}  // namespace hms
