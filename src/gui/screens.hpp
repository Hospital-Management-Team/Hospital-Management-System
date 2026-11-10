#pragma once
// The screens of the graphical front end, and which role sees which of them.

#include <memory>
#include <string>
#include <vector>

#include "auth/authentication.hpp"
#include "gui/context.hpp"

namespace hms::gui {

// Patients and appointments (Reception, also read-only for the Doctor)
std::unique_ptr<Screen> makeRegisterScreen();
std::unique_ptr<Screen> makePatientsScreen();
std::unique_ptr<Screen> makeBookScreen();
std::unique_ptr<Screen> makeChangeAppointmentScreen();
std::unique_ptr<Screen> makeRemindersScreen();

// Billing (Reception)
std::unique_ptr<Screen> makeBillVisitScreen();
std::unique_ptr<Screen> makePaymentScreen();
std::unique_ptr<Screen> makeBillsScreen();
std::unique_ptr<Screen> makeReceiptScreen();

// Lab Technician and Pharmacist
std::unique_ptr<Screen> makeLabPendingScreen();
std::unique_ptr<Screen> makeLabPatientScreen();
std::unique_ptr<Screen> makeStockScreen();
std::unique_ptr<Screen> makeDispenseScreen();
std::unique_ptr<Screen> makePatientPrescriptionsScreen();

// Administrator
std::unique_ptr<Screen> makeStaffScreen();
std::unique_ptr<Screen> makeDoctorHoursScreen();
std::unique_ptr<Screen> makeFeesScreen();
std::unique_ptr<Screen> makeMasterDataScreen();
std::unique_ptr<Screen> makeReportsScreen();
std::unique_ptr<Screen> makeAuditLogScreen();
std::unique_ptr<Screen> makeSettingsScreen();

// Doctor
std::unique_ptr<Screen> makeMyAppointmentsScreen();
std::unique_ptr<Screen> makeRecordConsultationScreen();
std::unique_ptr<Screen> makePastConsultationsScreen();
std::unique_ptr<Screen> makeMedicalHistoryScreen();
std::unique_ptr<Screen> makeLabResultsScreen();
std::unique_ptr<Screen> makeMyHoursScreen();

struct NavEntry {
    std::string label;
    std::unique_ptr<Screen> screen;
};

// The menu of a role: every entry is a screen, opened from the list at the left.
std::vector<NavEntry> navigationFor(auth::Role role);

}  // namespace hms::gui
