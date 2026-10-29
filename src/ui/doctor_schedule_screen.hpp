#pragma once
// Admin screen: a doctor's department and weekly hours (HMS-F-020, minimal;
// issue #4). Holds no business rules; AppointmentService checks the role,
// validates and audits.

#include "appointment/appointment_service.hpp"
#include "auth/session.hpp"
#include "ui/console_io.hpp"

namespace hms {

void doctorScheduleScreen(ConsoleIO& io, AppointmentService& appointments, const Session& session);

// Issue #5: a Doctor keeps their own weekly hours up to date.
void ownHoursScreen(ConsoleIO& io, AppointmentService& appointments, const Session& session);

}  // namespace hms
