#pragma once
// Fixtures for the billing tests (issue #7), on top of the consultation
// fixtures (issue #5).

#include "billing/billing_service.hpp"
#include "consultation_test_support.hpp"

namespace hms::test {

struct BillingFixture : ConsultationFixture {
    BillingService billing{db(), clock, policy};

    void setFee(std::int64_t doctorId, const std::string& rupees) {
        auto r = billing.setConsultationFee(admin(), doctorId, rupees);
        REQUIRE_MESSAGE(r.ok(), (r.ok() ? "" : r.error().describe()));
    }

    // A visit the doctor has recorded: booked, held and the consultation saved, with `tests` ordered.
    AppointmentRecord completedVisit(std::int64_t uhid, std::int64_t doctorId, const std::string& doctorUsername,
                                     const std::vector<std::string>& tests = {},
                                     const LocalSlot& slot = at(kMonday, 9, 0)) {
        auto a = visit(uhid, doctorId, slot);
        auto d = draft(a.id);
        for (const auto& t : tests) d.labOrders.push_back({t, ""});
        auto r = consultations.recordConsultation(doctorSession(doctorUsername), d);
        REQUIRE_MESSAGE(r.ok(), (r.ok() ? "" : r.error().describe()));
        return a;
    }

    Bill billOk(std::int64_t appointmentId, const std::vector<std::string>& testPrices = {},
                const std::vector<ExtraChargeDraft>& extras = {}) {
        auto r = billing.generateBill(receptionist(), appointmentId, testPrices, extras);
        REQUIRE_MESSAGE(r.ok(), (r.ok() ? "" : r.error().describe()));
        return r.value();
    }
};

}  // namespace hms::test
