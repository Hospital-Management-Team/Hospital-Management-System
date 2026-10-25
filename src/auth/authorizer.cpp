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
    }
    return false;  // deny anything not listed
}

std::string auditActionFor(Permission permission) {
    switch (permission) {
        case Permission::RegisterPatient: return audit_action::kPatientRegister;
        case Permission::UpdatePatientDemographics: return audit_action::kPatientUpdateDemographics;
        case Permission::SearchPatients: return audit_action::kPatientSearch;
        case Permission::ViewPatientDemographics: return audit_action::kPatientView;
    }
    return "UNKNOWN";
}

}  // namespace hms
