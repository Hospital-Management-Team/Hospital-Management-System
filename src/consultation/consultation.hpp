#pragma once
// Consultation Module - data types (SRS HMS-F-022; issue #5).
//
// A consultation is the doctor's record of one appointment: the diagnosis, the
// prescription lines and the recommended laboratory tests. It is linked to the
// patient and the appointment it came from. Medicine and test names are free
// text here; laboratory results and dispensing (issue #8) are linked to these
// rows later.

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "appointment/appointment.hpp"

namespace hms {

// What the doctor enters. Numbers arrive as text so that the service can
// refuse "abc" or "0" with a clear message.
struct PrescriptionDraft {
    std::string medicineName;
    std::string dosage;     // e.g. "1 tablet twice a day for 5 days"
    std::string quantity;   // total units to hand over
};

struct LabOrderDraft {
    std::string testName;
    std::string instructions;  // optional
};

struct ConsultationDraft {
    std::int64_t appointmentId = 0;
    std::string diagnosis;  // required
    std::vector<PrescriptionDraft> prescriptions;
    std::vector<LabOrderDraft> labOrders;
};

struct PrescriptionLine {
    std::int64_t itemId = 0;
    std::string medicineName;
    std::string dosage;
    std::int64_t quantity = 0;
};

struct LabOrderLine {
    std::int64_t orderId = 0;
    std::string testName;
    std::optional<std::string> instructions;
};

struct ConsultationRecord {
    std::int64_t consultationId = 0;
    std::int64_t appointmentId = 0;
    std::int64_t uhid = 0;
    std::string patientName;
    std::int64_t doctorStaffId = 0;
    std::string doctorUsername;
    LocalSlot slot;
    std::string diagnosis;
    std::string recordedAt;  // UTC, as stored
    std::vector<PrescriptionLine> prescriptions;
    std::vector<LabOrderLine> labOrders;
};

// Consultation numbers are shown as CON- plus at least six digits.
std::string formatConsultationId(std::int64_t id);

}  // namespace hms
