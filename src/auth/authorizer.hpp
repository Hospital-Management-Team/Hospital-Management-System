#pragma once
// Role-based access control (SRS HMS-SR-001, Appendix A).
// The check is made by the business logic before every protected operation,
// not only by hiding menu options. Rows are added as features arrive; the
// table below mirrors Appendix A for the operations that exist so far.

#include <string>

#include "auth/session.hpp"

namespace hms {

enum class Permission {
    RegisterPatient,           // HMS-F-001/002: Receptionist
    UpdatePatientDemographics, // HMS-F-005:     Receptionist
    SearchPatients,            // HMS-F-004:     Receptionist, Doctor (demographic data only)
    ViewPatientDemographics,   // HMS-F-004:     Receptionist, Doctor
    // Appointments (issue #4)
    ManageDoctorSchedule,      // HMS-F-020:     Admin
    ListDoctors,               // HMS-F-010/020: Receptionist, Admin
    BookAppointment,           // HMS-F-010/011: Receptionist
    ViewAppointments,          // HMS-F-010/012: Receptionist (free slots, a patient's appointments)
    CancelAppointment,         // HMS-F-012:     Receptionist
    RescheduleAppointment,     // HMS-F-012:     Receptionist
    GenerateReminders,         // HMS-F-013:     Receptionist
    ViewOwnQueue,              // HMS-F-021:     Doctor (own patients only)
};

bool isAllowed(Role role, Permission permission);

// Audit action name used when an attempt is denied.
std::string auditActionFor(Permission permission);

}  // namespace hms
