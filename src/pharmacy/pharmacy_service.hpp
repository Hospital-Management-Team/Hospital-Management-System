#pragma once
// Pharmacy Module business logic (SRS HMS-F-041; SAD 3.4; issue #8). Follows
// the Appointment, Consultation and Billing contract: the role is checked
// first (a denial is audited), the input is validated, and each change is made
// in one transaction together with its audit entry.
//
//  - Pharmacist: checks the stock, adds a medicine, restocks, corrects a name or
//    unit, sets the stock to a counted figure (the reason is kept), makes a medicine
//    unavailable or available again, lists the prescription lines still to
//    dispense, and dispenses against one line.
//  - An unavailable medicine can be neither dispensed nor restocked and raises no
//    low-stock alert; its history stays.
//  - Dispensing reduces the stock by exactly the dispensed quantity
//    (AC-HMS-F-041). It is refused when the stock is too low or when more than
//    the prescribed quantity would be given in total. Part of a line may be
//    dispensed now and the rest later.
//  - The doctor's medicine name is free text (D-43), so the pharmacist picks
//    the stock medicine that matches.
//  - Low-stock alert: shown when the stock is BELOW the configured threshold
//    (E-08). Audit entries hold IDs and field names only.

#include <string>
#include <vector>

#include "audit/audit_log.hpp"
#include "auth/authorizer.hpp"
#include "auth/session.hpp"
#include "config/policy.hpp"
#include "core/clock.hpp"
#include "core/result.hpp"
#include "data/database.hpp"
#include "pharmacy/pharmacy.hpp"
#include "pharmacy/pharmacy_repository.hpp"

namespace hms {

class PharmacyService {
public:
    PharmacyService(Database& db, const Clock& clock, AppPolicy policy);

    Result<std::vector<Medicine>> listStock(const Session& session);
    Result<Medicine> addMedicine(const Session& session, const MedicineDraft& draft);
    Result<Medicine> restock(const Session& session, std::int64_t medicineId, const std::string& quantityText);
    // Fixes the name or unit typed earlier.
    Result<Medicine> updateMedicine(const Session& session, std::int64_t medicineId, const MedicineEdit& edit);
    Result<Medicine> setMedicineActive(const Session& session, std::int64_t medicineId, bool active);
    // Sets the stock to the counted figure. The difference is saved as one movement with its reason.
    Result<Medicine> correctStock(const Session& session, std::int64_t medicineId, const std::string& countedText,
                                  const std::string& reason);

    // Lines with something left to dispense, oldest first.
    Result<std::vector<DispenseLine>> listPendingPrescriptions(const Session& session);
    Result<std::vector<DispenseLine>> listPrescriptionsForPatient(const Session& session, std::int64_t uhid);

    Result<DispenseOutcome> dispense(const Session& session, std::int64_t prescriptionItemId, std::int64_t medicineId,
                                     const std::string& quantityText);

    const AppPolicy& policy() const { return policy_; }

private:
    bool authorize(const Session& session, Permission permission);
    Error denied(const Session& session, const char* what) const;
    bool isLow(const Medicine& m) const { return m.active && m.stockQuantity < policy_.labPharmacy.lowStockThreshold; }

    Database& db_;
    const Clock& clock_;
    AppPolicy policy_;
    PharmacyRepository repo_;
    AuditLog audit_;
};

}  // namespace hms
