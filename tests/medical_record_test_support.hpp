#pragma once
// Fixtures for the medical record tests (issue #6), on top of the consultation
// fixtures (issue #5).

#include "consultation_test_support.hpp"
#include "record/medical_record_service.hpp"

namespace hms::test {

struct MedicalRecordFixture : ConsultationFixture {
    MedicalRecordService records{db(), clock, policy};

    static MedicalHistoryDraft history(const std::string& history, const std::string& allergies = "",
                                       const std::string& medications = "") {
        return MedicalHistoryDraft{history, allergies, medications};
    }

    MedicalHistoryVersion updateOk(const std::string& doctor, std::int64_t uhid, const MedicalHistoryDraft& d) {
        auto r = records.updateHistory(doctorSession(doctor), uhid, d);
        REQUIRE_MESSAGE(r.ok(), (r.ok() ? "" : r.error().describe()));
        return r.value();
    }
};

}  // namespace hms::test
