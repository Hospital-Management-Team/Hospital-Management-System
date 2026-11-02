#include "pharmacy/pharmacy_service.hpp"

#include "core/text.hpp"
#include "patient/uhid.hpp"
#include "validation/input.hpp"

namespace hms {

namespace {
constexpr const char* kName = "Medicine name";
constexpr const char* kUnit = "Unit";
constexpr const char* kInitialStock = "Opening stock";
constexpr const char* kQuantity = "Quantity";
constexpr const char* kCounted = "Counted stock";
constexpr const char* kReason = "Reason";
const char* kStorageFailed = "The data store reported an error. Nothing was saved. Please try again.";
const char* kReadFailed = "The data store reported an error. Please try again.";
const char* kOutcomeUnknown =
    "The data store could not confirm whether the change was saved. Do not enter it again. "
    "Restart the application and check the stock.";

// Runs a database action and turns storage failures into the error contract.
template <class T, class F>
Result<T> guarded(bool writes, F&& action) {
    try {
        return action();
    } catch (const CommitUncertain&) {
        return writes ? Error::outcomeUnknown(kOutcomeUnknown) : Error::storageFailure(kReadFailed);
    } catch (const DbError&) {
        return Error::storageFailure(writes ? kStorageFailed : kReadFailed);
    } catch (const InjectedFault&) {
        return Error::storageFailure(writes ? kStorageFailed : kReadFailed);
    }
}

Error medicineNotFound(std::int64_t id) {
    return Error::notFound("No medicine found with number " + formatMedicineId(id) + ".");
}
Error unavailable(const Medicine& m) {
    return Error::conflict(m.name + " is marked unavailable. Make it available first.");
}
Error lineNotFound(std::int64_t id) {
    return Error::notFound("No prescription line found with number " + formatPrescriptionId(id) + ".");
}

// A text field of at most `maxChars` characters, with runs of spaces collapsed.
Result<std::string> shortText(const std::string& raw, const char* field, std::size_t maxChars) {
    auto line = input::requireLine(raw, field);
    if (!line) return line.error();
    std::string value = text::collapseSpaces(line.value());
    const auto len = text::utf8Length(value);
    if (len > maxChars) {
        return Error::validation(field, "must be at most " + std::to_string(maxChars) + " characters (it has " +
                                            std::to_string(len) + ")");
    }
    return value;
}
}  // namespace

PharmacyService::PharmacyService(Database& db, const Clock& clock, AppPolicy policy)
    : db_(db), clock_(clock), policy_(policy), repo_(db), audit_(db, clock) {}

bool PharmacyService::authorize(const Session& session, Permission permission) {
    if (isAllowed(session.role, permission)) return true;
    try {
        Transaction tx(db_);
        AuditEntryInput e;
        e.action = auditActionFor(permission);
        e.outcome = AuditOutcome::Denied;
        audit_.append(session.userId, e);
        tx.commit();
    } catch (const std::exception&) {
        // The denial stands even if it could not be recorded.
    }
    return false;
}

Error PharmacyService::denied(const Session& session, const char* what) const {
    return Error::accessDenied("Your role (" + to_string(session.role) + ") may not " + what + ".");
}

Result<std::vector<Medicine>> PharmacyService::listStock(const Session& session) {
    if (!authorize(session, Permission::ViewStock)) return denied(session, "view the medicine stock");
    return guarded<std::vector<Medicine>>(false, [&]() -> Result<std::vector<Medicine>> {
        auto rows = repo_.listMedicines();
        for (auto& m : rows) m.lowStock = isLow(m);
        return rows;
    });
}

Result<Medicine> PharmacyService::addMedicine(const Session& session, const MedicineDraft& draft) {
    if (!authorize(session, Permission::AddMedicine)) return denied(session, "change the medicine stock");
    const auto& lp = policy_.labPharmacy;
    std::vector<FieldError> errors;
    std::string name, unit;
    std::int64_t opening = 0;
    if (auto v = shortText(draft.name, kName, lp.maxMedicineNameChars)) name = v.value();
    else errors.push_back(v.error().fields.empty() ? FieldError{kName, v.error().message} : v.error().fields.front());
    if (auto v = shortText(draft.unit, kUnit, lp.maxUnitChars)) unit = v.value();
    else errors.push_back(v.error().fields.empty() ? FieldError{kUnit, v.error().message} : v.error().fields.front());
    const std::string stockText = text::trimSpaces(draft.initialStock);
    if (!stockText.empty()) {
        auto n = input::parseWholeNumber(stockText, kInitialStock, 0, lp.maxStockQuantity);
        if (n) opening = n.value();
        else errors.push_back(n.error().fields.empty() ? FieldError{kInitialStock, n.error().message} : n.error().fields.front());
    }
    if (!errors.empty()) return Error::validation(std::move(errors));

    return guarded<Medicine>(true, [&]() -> Result<Medicine> {
        Transaction tx(db_);
        if (repo_.medicineNameExists(name)) {
            return Error::conflict("A medicine named '" + name + "' is already in the stock list.");
        }
        const std::string now = toIsoUtc(clock_.now());
        const std::int64_t id = repo_.insertMedicine(name, unit, now);
        if (opening > 0) repo_.moveStock(id, opening, "INITIAL", std::nullopt, session.userId, now);
        AuditEntryInput e;
        e.action = audit_action::kMedicineAdd;
        e.recordId = formatMedicineId(id);
        e.changedFields = {"name", "unit", "stock"};
        audit_.append(session.userId, e);
        db_.faultPoint("medicine_before_commit");
        tx.commit();
        auto m = *repo_.findMedicine(id);
        m.lowStock = isLow(m);
        return m;
    });
}

Result<Medicine> PharmacyService::restock(const Session& session, std::int64_t medicineId,
                                          const std::string& quantityText) {
    if (!authorize(session, Permission::RestockMedicine)) return denied(session, "change the medicine stock");
    auto qty = input::parseWholeNumber(text::trimSpaces(quantityText), kQuantity, 1, policy_.labPharmacy.maxStockQuantity);
    if (!qty) return qty.error();

    return guarded<Medicine>(true, [&]() -> Result<Medicine> {
        Transaction tx(db_);
        auto m = repo_.findMedicine(medicineId);
        if (!m) return medicineNotFound(medicineId);
        if (!m->active) return unavailable(*m);
        if (m->stockQuantity + qty.value() > policy_.labPharmacy.maxStockQuantity) {
            return Error::validation(kQuantity, "would take the stock above " +
                                                    std::to_string(policy_.labPharmacy.maxStockQuantity));
        }
        repo_.moveStock(medicineId, qty.value(), "RESTOCK", std::nullopt, session.userId, toIsoUtc(clock_.now()));
        AuditEntryInput e;
        e.action = audit_action::kStockRestock;
        e.recordId = formatMedicineId(medicineId);
        e.changedFields = {"stock"};
        audit_.append(session.userId, e);
        db_.faultPoint("restock_before_commit");
        tx.commit();
        auto after = *repo_.findMedicine(medicineId);
        after.lowStock = isLow(after);
        return after;
    });
}

Result<Medicine> PharmacyService::updateMedicine(const Session& session, std::int64_t medicineId,
                                                 const MedicineEdit& edit) {
    if (!authorize(session, Permission::EditMedicine)) return denied(session, "change a medicine");
    const auto& lp = policy_.labPharmacy;
    std::vector<FieldError> errors;
    std::string name, unit;
    if (auto v = shortText(edit.name, kName, lp.maxMedicineNameChars)) name = v.value();
    else errors.push_back(v.error().fields.empty() ? FieldError{kName, v.error().message} : v.error().fields.front());
    if (auto v = shortText(edit.unit, kUnit, lp.maxUnitChars)) unit = v.value();
    else errors.push_back(v.error().fields.empty() ? FieldError{kUnit, v.error().message} : v.error().fields.front());
    if (!errors.empty()) return Error::validation(std::move(errors));

    return guarded<Medicine>(true, [&]() -> Result<Medicine> {
        Transaction tx(db_);
        auto m = repo_.findMedicine(medicineId);
        if (!m) return medicineNotFound(medicineId);
        if (m->name == name && m->unit == unit) return Error::conflict("The name and unit are the same as before; nothing to change.");
        if (repo_.medicineNameExists(name, medicineId)) {
            return Error::conflict("A medicine named '" + name + "' is already in the stock list.");
        }
        repo_.updateMedicine(medicineId, name, unit);
        AuditEntryInput e;
        e.action = audit_action::kMedicineUpdate;
        e.recordId = formatMedicineId(medicineId);
        e.changedFields = {"name", "unit"};
        audit_.append(session.userId, e);
        db_.faultPoint("medicine_update_before_commit");
        tx.commit();
        auto after = *repo_.findMedicine(medicineId);
        after.lowStock = isLow(after);
        return after;
    });
}

Result<Medicine> PharmacyService::setMedicineActive(const Session& session, std::int64_t medicineId, bool active) {
    if (!authorize(session, Permission::EditMedicine)) return denied(session, "change a medicine");
    return guarded<Medicine>(true, [&]() -> Result<Medicine> {
        Transaction tx(db_);
        auto m = repo_.findMedicine(medicineId);
        if (!m) return medicineNotFound(medicineId);
        if (m->active == active) {
            return Error::conflict(active ? "That medicine is already available." : "That medicine is already unavailable.");
        }
        repo_.setActive(medicineId, active);
        AuditEntryInput e;
        e.action = audit_action::kMedicineUpdate;
        e.recordId = formatMedicineId(medicineId);
        e.changedFields = {"active"};
        audit_.append(session.userId, e);
        db_.faultPoint("medicine_update_before_commit");
        tx.commit();
        auto after = *repo_.findMedicine(medicineId);
        after.lowStock = isLow(after);
        return after;
    });
}

Result<Medicine> PharmacyService::correctStock(const Session& session, std::int64_t medicineId,
                                               const std::string& countedText, const std::string& reason) {
    if (!authorize(session, Permission::CorrectStock)) return denied(session, "change the medicine stock");
    std::vector<FieldError> errors;
    std::int64_t counted = 0;
    auto n = input::parseWholeNumber(text::trimSpaces(countedText), kCounted, 0, policy_.labPharmacy.maxStockQuantity);
    if (n) counted = n.value();
    else errors.push_back(n.error().fields.empty() ? FieldError{kCounted, n.error().message} : n.error().fields.front());
    std::string why;
    if (auto v = shortText(reason, kReason, policy_.labPharmacy.maxReasonChars)) why = v.value();
    else errors.push_back(v.error().fields.empty() ? FieldError{kReason, v.error().message} : v.error().fields.front());
    if (!errors.empty()) return Error::validation(std::move(errors));

    return guarded<Medicine>(true, [&]() -> Result<Medicine> {
        Transaction tx(db_);
        auto m = repo_.findMedicine(medicineId);
        if (!m) return medicineNotFound(medicineId);
        const std::int64_t delta = counted - m->stockQuantity;
        if (delta == 0) return Error::conflict("The stock is already " + std::to_string(counted) + "; nothing to correct.");
        repo_.moveStock(medicineId, delta, "CORRECTION", std::nullopt, session.userId, toIsoUtc(clock_.now()), why);
        AuditEntryInput e;
        e.action = audit_action::kStockCorrect;
        e.recordId = formatMedicineId(medicineId);
        e.changedFields = {"stock"};
        audit_.append(session.userId, e);
        db_.faultPoint("stock_correct_before_commit");
        tx.commit();
        auto after = *repo_.findMedicine(medicineId);
        after.lowStock = isLow(after);
        return after;
    });
}

Result<std::vector<DispenseLine>> PharmacyService::listPendingPrescriptions(const Session& session) {
    if (!authorize(session, Permission::ViewPrescriptions)) return denied(session, "view prescriptions");
    return guarded<std::vector<DispenseLine>>(false, [&]() -> Result<std::vector<DispenseLine>> {
        return repo_.listPending(policy_.labPharmacy.maxListRows);
    });
}

Result<std::vector<DispenseLine>> PharmacyService::listPrescriptionsForPatient(const Session& session,
                                                                                   std::int64_t uhid) {
    if (!authorize(session, Permission::ViewPrescriptions)) return denied(session, "view prescriptions");
    return guarded<std::vector<DispenseLine>>(false, [&]() -> Result<std::vector<DispenseLine>> {
        if (!repo_.patientExists(uhid)) return Error::notFound("No patient found with UHID " + formatUhid(uhid) + ".");
        return repo_.listForPatient(uhid);
    });
}

Result<DispenseOutcome> PharmacyService::dispense(const Session& session, std::int64_t prescriptionItemId,
                                                  std::int64_t medicineId, const std::string& quantityText) {
    if (!authorize(session, Permission::DispenseMedicine)) return denied(session, "dispense medicine");
    auto qty = input::parseWholeNumber(text::trimSpaces(quantityText), kQuantity, 1, policy_.labPharmacy.maxStockQuantity);
    if (!qty) return qty.error();

    return guarded<DispenseOutcome>(true, [&]() -> Result<DispenseOutcome> {
        Transaction tx(db_);
        auto line = repo_.findLine(prescriptionItemId);
        if (!line) return lineNotFound(prescriptionItemId);
        auto med = repo_.findMedicine(medicineId);
        if (!med) return medicineNotFound(medicineId);
        if (!med->active) return unavailable(*med);
        if (line->remaining() <= 0) {
            return Error::conflict("Prescription line " + formatPrescriptionId(prescriptionItemId) +
                                   " has already been dispensed in full.");
        }
        if (qty.value() > line->remaining()) {
            return Error::conflict("Only " + std::to_string(line->remaining()) + " of " +
                                   std::to_string(line->prescribed) + " are still to be dispensed on this line.");
        }
        if (qty.value() > med->stockQuantity) {
            return Error::conflict("Only " + std::to_string(med->stockQuantity) + " " + med->unit + " of " +
                                   med->name + " in stock. Nothing was dispensed.");
        }
        const std::string now = toIsoUtc(clock_.now());
        const std::int64_t dispenseId = repo_.insertDispense(prescriptionItemId, medicineId, qty.value(), session.userId, now);
        repo_.moveStock(medicineId, -qty.value(), "DISPENSE", dispenseId, session.userId, now);
        AuditEntryInput e;
        e.action = audit_action::kMedicineDispense;
        e.recordId = formatPrescriptionId(prescriptionItemId);
        e.changedFields = {"dispensed_quantity", "stock"};
        audit_.append(session.userId, e);
        db_.faultPoint("dispense_before_commit");
        tx.commit();

        DispenseOutcome out;
        out.dispenseId = dispenseId;
        out.itemId = prescriptionItemId;
        out.medicineId = medicineId;
        out.medicineName = med->name;
        out.unit = med->unit;
        out.quantity = qty.value();
        out.remainingToDispense = line->remaining() - qty.value();
        out.stockAfter = med->stockQuantity - qty.value();
        out.threshold = policy_.labPharmacy.lowStockThreshold;
        Medicine after = *med;
        after.stockQuantity = out.stockAfter;
        out.lowStock = isLow(after);
        return out;
    });
}

}  // namespace hms
