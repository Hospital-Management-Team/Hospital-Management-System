#pragma once
// Fixtures for the consultation tests (issue #5), on top of the appointment
// fixtures (issue #4).

#include "appointment_test_support.hpp"
#include "consultation/consultation_service.hpp"

namespace hms::test {

struct ConsultationFixture : AppointmentFixture {
    ConsultationService consultations{db(), clock, policy};

    // Books Monday 26-10-2026 09:00 for a patient and moves the clock to 09:30 that day,
    // so the visit has taken place and can be recorded.
    AppointmentRecord visit(std::int64_t uhid, std::int64_t doctorId, const LocalSlot& start = at(kMonday, 9, 0)) {
        auto a = bookOk(uhid, doctorId, start);
        setLocalTime(start.date, 9, 30);
        return a;
    }

    static ConsultationDraft draft(std::int64_t appointmentId, const std::string& diagnosis = "Viral fever") {
        ConsultationDraft d;
        d.appointmentId = appointmentId;
        d.diagnosis = diagnosis;
        return d;
    }
};

}  // namespace hms::test
