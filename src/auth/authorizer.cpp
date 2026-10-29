#include "auth/authorizer.hpp"

#include "audit/audit_log.hpp"

namespace hms {

std::string to_string(Role role) {
    switch (role) {
        case Role::Receptionist: return "Receptionist";
        case Role::Doctor: return "Doctor";
        case Role::LabTechnician: return "Lab Technician";
        case Role::Pharmacist: return "Pharmacist";
        case Role::Admin: return "Admin";
    }
    return "Unknown";
}

bool isAllowed(Role role, Permission permission) {
    switch (permission) {
        case Permission::RegisterPatient:
        case Permission::UpdatePatientDemographics:
            return role == Role::Receptionist;
        case Permission::SearchPatients:
        case Permission::ViewPatientDemographics:
            return role == Role::Receptionist || role == Role::Doctor;
        case Permission::ManageDoctorSchedule:
            return role == Role::Admin;
        case Permission::ListDoctors:
            return role == Role::Receptionist || role == Role::Admin;
        case Permission::BookAppointment:
        case Permission::ViewAppointments:
        case Permission::CancelAppointment:
        case Permission::RescheduleAppointment:
        case Permission::GenerateReminders:
            return role == Role::Receptionist;
        case Permission::ViewOwnQueue:
            return role == Role::Doctor;
    }
    return false;  // deny anything not listed
}

std::string auditActionFor(Permission permission) {
    switch (permission) {
        case Permission::RegisterPatient: return audit_action::kPatientRegister;
        case Permission::UpdatePatientDemographics: return audit_action::kPatientUpdateDemographics;
        case Permission::SearchPatients: return audit_action::kPatientSearch;
        case Permission::ViewPatientDemographics: return audit_action::kPatientView;
        case Permission::ManageDoctorSchedule: return audit_action::kDoctorScheduleSet;
        case Permission::ListDoctors: return audit_action::kDoctorList;
        case Permission::BookAppointment: return audit_action::kAppointmentBook;
        case Permission::ViewAppointments: return audit_action::kAppointmentView;
        case Permission::CancelAppointment: return audit_action::kAppointmentCancel;
        case Permission::RescheduleAppointment: return audit_action::kAppointmentReschedule;
        case Permission::GenerateReminders: return audit_action::kAppointmentReminder;
        case Permission::ViewOwnQueue: return audit_action::kAppointmentQueueView;
    }
    return "UNKNOWN";
}

}  // namespace hms
