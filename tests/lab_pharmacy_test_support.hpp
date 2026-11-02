#pragma once
// Fixtures for the laboratory and pharmacy tests (issue #8), on top of the
// consultation fixtures (issue #5).

#include "consultation_test_support.hpp"
#include "lab/lab_service.hpp"
#include "pharmacy/pharmacy_service.hpp"

namespace hms::test {

inline Session labTech(const std::string& username = "lab1") {
    return Session{username, username, Role::LabTechnician, false};
}
inline Session pharmacistSession(const std::string& username = "pharm1") {
    return Session{username, username, Role::Pharmacist, false};
}

struct LabPharmacyFixture : ConsultationFixture {
    LabService lab{db(), clock, policy};
    PharmacyService pharmacy{db(), clock, policy};

    // A held visit whose consultation is recorded with the given tests and medicines.
    ConsultationRecord seen(std::int64_t uhid, std::int64_t doctorId, const std::string& doctorUsername,
                            const std::vector<std::string>& tests = {},
                            const std::vector<PrescriptionDraft>& medicines = {},
                            const LocalSlot& slot = at(kMonday, 9, 0)) {
        auto a = visit(uhid, doctorId, slot);
        auto d = draft(a.id);
        for (const auto& t : tests) d.labOrders.push_back({t, ""});
        d.prescriptions = medicines;
        auto r = consultations.recordConsultation(doctorSession(doctorUsername), d);
        REQUIRE_MESSAGE(r.ok(), (r.ok() ? "" : r.error().describe()));
        return r.value();
    }

    Medicine addMedicine(const std::string& name, const std::string& unit, const std::string& stock) {
        auto r = pharmacy.addMedicine(pharmacistSession(), MedicineDraft{name, unit, stock});
        REQUIRE_MESSAGE(r.ok(), (r.ok() ? "" : r.error().describe()));
        return r.value();
    }

    std::int64_t stockOf(std::int64_t medicineId) {
        return count("SELECT stock_quantity FROM medicine WHERE medicine_id = " + std::to_string(medicineId));
    }
};

}  // namespace hms::test
