#pragma once
// Medical Records Module - data types (SRS HMS-F-003; issue #6).
//
// A patient's medical history is kept as versions. Each update by a doctor
// saves a new version holding the history, the known allergies and the current
// medications, with the doctor and the time; earlier versions are never changed
// or deleted. The current-medications list is maintained by the doctor here:
// prescribing (HMS-F-022) and dispensing (HMS-F-041) never change it.

#include <cstdint>
#include <string>
#include <vector>

namespace hms {

// What the doctor enters. Each field may be empty, but a version must differ
// from the one before it.
struct MedicalHistoryDraft {
    std::string history;
    std::string allergies;
    std::string currentMedications;
};

struct MedicalHistoryVersion {
    std::int64_t versionId = 0;
    std::int64_t uhid = 0;
    std::string history;
    std::string allergies;
    std::string currentMedications;
    std::int64_t authorStaffId = 0;
    std::string authorUsername;
    std::string recordedAt;  // UTC, as stored
};

// An opened medical history: every version, newest first. A patient with no
// version yet has an empty list ("No medical history recorded").
struct MedicalHistory {
    std::int64_t uhid = 0;
    std::string patientName;
    std::vector<MedicalHistoryVersion> versions;

    const MedicalHistoryVersion* current() const { return versions.empty() ? nullptr : &versions.front(); }
};

// Version numbers are shown as MHV- plus at least six digits.
std::string formatHistoryVersionId(std::int64_t id);

}  // namespace hms
