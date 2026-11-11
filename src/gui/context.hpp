#pragma once
// What every screen of the graphical front end can reach: the services (the same ones
// the console screens use), the logged-in session, and a few pieces of shared state.

#include <cstdint>
#include <memory>
#include <string>
#include <utility>

#include "admin/admin_service.hpp"
#include "appointment/appointment_service.hpp"
#include "audit/audit_log_service.hpp"
#include "auth/authentication.hpp"
#include "auth/session.hpp"
#include "billing/billing_service.hpp"
#include "config/policy.hpp"
#include "consultation/consultation_service.hpp"
#include "core/clock.hpp"
#include "data/database.hpp"
#include "gui/ui.hpp"
#include "lab/lab_service.hpp"
#include "patient/patient_service.hpp"
#include "pharmacy/pharmacy_service.hpp"
#include "record/medical_record_service.hpp"

namespace hms::gui {

struct Services {
    Services(Database& database, const Clock& clk, const AppPolicy& pol);

    Database& db;
    const Clock& clock;
    AppPolicy policy;
    PatientService patients;
    AppointmentService appointments;
    ConsultationService consultations;
    BillingService billing;
    MedicalRecordService records;
    AuditLogService audit;
    AdminService admin;
    LabService lab;
    PharmacyService pharmacy;
};

struct Context {
    Context(Services& services, Session sess, auth::Role r) : svc(services), session(std::move(sess)), role(r) {}

    Services& svc;
    Session session;
    auth::Role role;

    // The patient last looked up on any screen; other screens offer to use the same one.
    std::int64_t currentUhid = 0;
    // The appointment a screen hands to the next one (for example "record a consultation").
    std::int64_t appointmentForNext = 0;
    // The bill a screen hands to the next one (for example "record a payment").
    std::int64_t billForNext = 0;
    // A screen sets this to the label of the screen it wants to open next.
    std::string goTo;
    // SRS 2.5: after a save whose result is not known, no further change is allowed in this session.
    bool changesBlocked = false;

    Date today() const { return svc.patients.today(); }

    // Shows a failure on the notice; an OutcomeUnknown failure also blocks further changes.
    void fail(Notice& notice, const Error& e);
    // True (with the reason on the notice) when changes are blocked.
    bool blocked(Notice& notice) const;
};

// One screen of the main window (the content next to the menu).
class Screen {
public:
    virtual ~Screen() = default;
    virtual void draw(Context& ctx) = 0;
    // Called each time the screen is chosen from the menu.
    virtual void onOpen(Context& ctx) { (void)ctx; }
};

}  // namespace hms::gui
