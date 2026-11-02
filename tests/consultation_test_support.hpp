#pragma once
// Fixtures for the consultation tests (issue #5), on top of the appointment
// fixtures (issue #4).

#include "appointment_test_support.hpp"
#include "consultation/consultation_service.hpp"

namespace hms::test {

struct ConsultationFixture : AppointmentFixture {
    ConsultationService consultations{db(), clock, policy};

    // Books a visit (default Monday 26-10-2026 09:00) and moves the clock to 30 minutes
    // after its start, so the visit has taken place and can be recorded.
    AppointmentRecord visit(std::int64_t uhid, std::int64_t doctorId, const LocalSlot& start = at(kMonday, 9, 0)) {
        auto a = bookOk(uhid, doctorId, start);
        const int held = start.minute + 30;
        setLocalTime(start.date, held / 60, held % 60);
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
