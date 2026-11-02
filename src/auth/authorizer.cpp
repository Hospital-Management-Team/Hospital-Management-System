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
        case Permission::ManageOwnHours:
        case Permission::RecordConsultation:
        case Permission::ViewConsultations:
            return role == Role::Doctor;
        case Permission::SetConsultationFee:
            return role == Role::Admin;
        case Permission::GenerateBill:
        case Permission::ViewBills:
        case Permission::RecordPayment:
        case Permission::GenerateReceipt:
            return role == Role::Receptionist;
        case Permission::ViewMedicalHistory:
        case Permission::UpdateMedicalHistory:
            return role == Role::Doctor;
        case Permission::ViewAuditLog:
            return role == Role::Admin;
        case Permission::ViewLabOrders:
        case Permission::RecordLabResult:
            return role == Role::LabTechnician;
        case Permission::ViewLabResults:
            return role == Role::Doctor;
        case Permission::ViewStock:
        case Permission::AddMedicine:
        case Permission::RestockMedicine:
        case Permission::EditMedicine:
        case Permission::CorrectStock:
        case Permission::ViewPrescriptions:
        case Permission::DispenseMedicine:
            return role == Role::Pharmacist;
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
        case Permission::ManageOwnHours: return audit_action::kDoctorScheduleSet;
        case Permission::RecordConsultation: return audit_action::kConsultationRecord;
        case Permission::ViewConsultations: return audit_action::kConsultationView;
        case Permission::SetConsultationFee: return audit_action::kConsultationFeeSet;
        case Permission::GenerateBill: return audit_action::kBillGenerate;
        case Permission::ViewBills: return audit_action::kBillView;
        case Permission::RecordPayment: return audit_action::kPaymentRecord;
        case Permission::GenerateReceipt: return audit_action::kReceiptGenerate;
        case Permission::ViewMedicalHistory: return audit_action::kMedicalHistoryView;
        case Permission::UpdateMedicalHistory: return audit_action::kMedicalHistoryUpdate;
        case Permission::ViewAuditLog: return audit_action::kAuditLogView;
        case Permission::ViewLabOrders: return audit_action::kLabOrderView;
        case Permission::RecordLabResult: return audit_action::kLabResultRecord;
        case Permission::ViewLabResults: return audit_action::kLabResultView;
        case Permission::ViewStock: return audit_action::kStockView;
        case Permission::AddMedicine: return audit_action::kMedicineAdd;
        case Permission::RestockMedicine: return audit_action::kStockRestock;
        case Permission::EditMedicine: return audit_action::kMedicineUpdate;
        case Permission::CorrectStock: return audit_action::kStockCorrect;
        case Permission::ViewPrescriptions: return audit_action::kPrescriptionView;
        case Permission::DispenseMedicine: return audit_action::kMedicineDispense;
    }
    return "UNKNOWN";
}

}  // namespace hms
