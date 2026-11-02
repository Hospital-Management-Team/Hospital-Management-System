#include "pharmacy/pharmacy_repository.hpp"

namespace hms {

namespace {
const char* kLineSelect =
    "SELECT i.item_id, i.consultation_id, c.uhid, p.full_name, d.username, i.medicine_name, i.dosage, i.quantity, "
    "COALESCE((SELECT SUM(x.quantity) FROM dispense x WHERE x.item_id = i.item_id), 0), c.recorded_at "
    "FROM prescription_item i "
    "JOIN consultation c ON c.consultation_id = i.consultation_id "
    "JOIN patient p ON p.uhid = c.uhid "
    "JOIN staff_accounts d ON d.staff_id = c.doctor_staff_id ";

DispenseLine readLine(const Statement& st) {
    DispenseLine l;
    l.itemId = st.getInt64(0);
    l.consultationId = st.getInt64(1);
    l.uhid = st.getInt64(2);
    l.patientName = st.getText(3);
    l.doctorUsername = st.getText(4);
    l.medicineName = st.getText(5);
    l.dosage = st.getText(6);
    l.prescribed = st.getInt64(7);
    l.dispensed = st.getInt64(8);
    l.prescribedAt = st.getText(9);
    return l;
}

Medicine readMedicine(const Statement& st) {
    Medicine m;
    m.medicineId = st.getInt64(0);
    m.name = st.getText(1);
    m.unit = st.getText(2);
    m.stockQuantity = st.getInt64(3);
    m.active = st.getInt64(4) == 1;
    return m;
}
}  // namespace

std::vector<Medicine> PharmacyRepository::listMedicines() {
    Statement st = db_.prepare("SELECT medicine_id, name, unit, stock_quantity, active FROM medicine ORDER BY name");
    std::vector<Medicine> out;
    while (st.step()) out.push_back(readMedicine(st));
    return out;
}

std::optional<Medicine> PharmacyRepository::findMedicine(std::int64_t medicineId) {
    Statement st = db_.prepare("SELECT medicine_id, name, unit, stock_quantity, active FROM medicine WHERE medicine_id = ?1");
    st.bind(1, medicineId);
    if (!st.step()) return std::nullopt;
    return readMedicine(st);
}

bool PharmacyRepository::medicineNameExists(const std::string& name, std::optional<std::int64_t> exceptId) {
    Statement st = db_.prepare("SELECT 1 FROM medicine WHERE name = ?1 AND (?2 IS NULL OR medicine_id <> ?2)");
    st.bind(1, name);
    st.bind(2, exceptId);
    return st.step();
}

void PharmacyRepository::updateMedicine(std::int64_t medicineId, const std::string& name, const std::string& unit) {
    Statement st = db_.prepare("UPDATE medicine SET name = ?1, unit = ?2 WHERE medicine_id = ?3");
    st.bind(1, name);
    st.bind(2, unit);
    st.bind(3, medicineId);
    st.step();
}

void PharmacyRepository::setActive(std::int64_t medicineId, bool active) {
    Statement st = db_.prepare("UPDATE medicine SET active = ?1 WHERE medicine_id = ?2");
    st.bind(1, active ? 1 : 0);
    st.bind(2, medicineId);
    st.step();
}

std::int64_t PharmacyRepository::insertMedicine(const std::string& name, const std::string& unit,
                                                const std::string& nowUtc) {
    Statement st = db_.prepare("INSERT INTO medicine (name, unit, stock_quantity, created_at) VALUES (?1, ?2, 0, ?3)");
    st.bind(1, name);
    st.bind(2, unit);
    st.bind(3, nowUtc);
    st.step();
    return db_.lastInsertRowId();
}

void PharmacyRepository::moveStock(std::int64_t medicineId, std::int64_t delta, const char* reason,
                                   std::optional<std::int64_t> dispenseId, const std::string& recordedBy,
                                   const std::string& nowUtc, const std::optional<std::string>& note) {
    Statement up = db_.prepare("UPDATE medicine SET stock_quantity = stock_quantity + ?1 WHERE medicine_id = ?2");
    up.bind(1, delta);
    up.bind(2, medicineId);
    up.step();
    Statement mv = db_.prepare(
        "INSERT INTO stock_movement (medicine_id, delta, reason, dispense_id, note, recorded_by, recorded_at) "
        "VALUES (?1, ?2, ?3, ?4, ?7, ?5, ?6)");
    mv.bind(1, medicineId);
    mv.bind(2, delta);
    mv.bind(3, reason);
    mv.bind(4, dispenseId);
    mv.bind(5, recordedBy);
    mv.bind(6, nowUtc);
    mv.bind(7, note);
    mv.step();
}

std::int64_t PharmacyRepository::movementTotal(std::int64_t medicineId) {
    Statement st = db_.prepare("SELECT COALESCE(SUM(delta), 0) FROM stock_movement WHERE medicine_id = ?1");
    st.bind(1, medicineId);
    st.step();
    return st.getInt64(0);
}

std::vector<DispenseLine> PharmacyRepository::query(const std::string& where, const std::string& tail,
                                                        const std::function<void(Statement&)>& bind) {
    Statement st = db_.prepare(std::string(kLineSelect) + where + " " + tail);
    bind(st);
    std::vector<DispenseLine> out;
    while (st.step()) out.push_back(readLine(st));
    return out;
}

std::optional<DispenseLine> PharmacyRepository::findLine(std::int64_t itemId) {
    auto rows = query("WHERE i.item_id = ?1", "", [&](Statement& st) { st.bind(1, itemId); });
    if (rows.empty()) return std::nullopt;
    return rows.front();
}

std::vector<DispenseLine> PharmacyRepository::listPending(std::size_t limit) {
    return query("WHERE i.quantity > COALESCE((SELECT SUM(x.quantity) FROM dispense x WHERE x.item_id = i.item_id), 0)",
                 "ORDER BY c.recorded_at, i.item_id LIMIT ?1",
                 [&](Statement& st) { st.bind(1, static_cast<std::int64_t>(limit)); });
}

std::vector<DispenseLine> PharmacyRepository::listForPatient(std::int64_t uhid) {
    return query("WHERE c.uhid = ?1", "ORDER BY i.item_id DESC", [&](Statement& st) { st.bind(1, uhid); });
}

bool PharmacyRepository::patientExists(std::int64_t uhid) {
    Statement st = db_.prepare("SELECT 1 FROM patient WHERE uhid = ?1");
    st.bind(1, uhid);
    return st.step();
}

std::int64_t PharmacyRepository::insertDispense(std::int64_t itemId, std::int64_t medicineId, std::int64_t quantity,
                                                const std::string& dispensedBy, const std::string& nowUtc) {
    Statement st = db_.prepare(
        "INSERT INTO dispense (item_id, medicine_id, quantity, dispensed_by, dispensed_at) VALUES (?1, ?2, ?3, ?4, ?5)");
    st.bind(1, itemId);
    st.bind(2, medicineId);
    st.bind(3, quantity);
    st.bind(4, dispensedBy);
    st.bind(5, nowUtc);
    st.step();
    return db_.lastInsertRowId();
}

}  // namespace hms
