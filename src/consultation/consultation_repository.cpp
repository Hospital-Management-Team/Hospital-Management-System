#include "consultation/consultation_repository.hpp"

namespace hms {

namespace {
const char* kSelect =
    "SELECT c.consultation_id, c.appointment_id, c.uhid, p.full_name, c.doctor_staff_id, s.username, "
    "       a.slot_start, c.diagnosis, c.recorded_at "
    "FROM consultation c "
    "JOIN patient p ON p.uhid = c.uhid "
    "JOIN staff_accounts s ON s.staff_id = c.doctor_staff_id "
    "JOIN appointment a ON a.appointment_id = c.appointment_id ";

ConsultationRecord readRecord(const Statement& st) {
    ConsultationRecord r;
    r.consultationId = st.getInt64(0);
    r.appointmentId = st.getInt64(1);
    r.uhid = st.getInt64(2);
    r.patientName = st.getText(3);
    r.doctorStaffId = st.getInt64(4);
    r.doctorUsername = st.getText(5);
    if (auto slot = LocalSlot::fromKey(st.getText(6))) r.slot = *slot;
    r.diagnosis = st.getText(7);
    r.recordedAt = st.getText(8);
    return r;
}
}  // namespace

std::string formatConsultationId(std::int64_t id) {
    std::string digits = std::to_string(id);
    if (digits.size() < 6) digits.insert(0, 6 - digits.size(), '0');
    return "CON-" + digits;
}

bool ConsultationRepository::patientExists(std::int64_t uhid) {
    auto st = db_.prepare("SELECT 1 FROM patient WHERE uhid = ?");
    st.bind(1, uhid);
    return st.step();
}

bool ConsultationRepository::existsForAppointment(std::int64_t appointmentId) {
    auto st = db_.prepare("SELECT 1 FROM consultation WHERE appointment_id = ?");
    st.bind(1, appointmentId);
    return st.step();
}

std::int64_t ConsultationRepository::insert(const ValidConsultation& c, const std::string& recordedBy,
                                            const std::string& nowUtc) {
    {
        auto st = db_.prepare(
            "INSERT INTO consultation (appointment_id, uhid, doctor_staff_id, diagnosis, recorded_by, recorded_at) "
            "VALUES (?, ?, ?, ?, ?, ?)");
        st.bind(1, c.appointmentId).bind(2, c.uhid).bind(3, c.doctorStaffId).bind(4, c.diagnosis)
            .bind(5, recordedBy).bind(6, nowUtc);
        st.run();
    }
    const std::int64_t id = db_.lastInsertRowId();
    for (const auto& rx : c.prescriptions) {
        auto st = db_.prepare(
            "INSERT INTO prescription_item (consultation_id, medicine_name, dosage, quantity) VALUES (?, ?, ?, ?)");
        st.bind(1, id).bind(2, rx.medicineName).bind(3, rx.dosage).bind(4, rx.quantity);
        st.run();
    }
    for (const auto& lab : c.labOrders) {
        auto st = db_.prepare(
            "INSERT INTO lab_order (consultation_id, test_name, instructions, ordered_at) VALUES (?, ?, ?, ?)");
        st.bind(1, id).bind(2, lab.testName).bind(3, lab.instructions).bind(4, nowUtc);
        st.run();
    }
    return id;
}

void ConsultationRepository::loadLines(ConsultationRecord& r) {
    {
        auto st = db_.prepare(
            "SELECT item_id, medicine_name, dosage, quantity FROM prescription_item WHERE consultation_id = ? "
            "ORDER BY item_id");
        st.bind(1, r.consultationId);
        while (st.step()) {
            r.prescriptions.push_back(PrescriptionLine{st.getInt64(0), st.getText(1), st.getText(2), st.getInt64(3)});
        }
    }
    auto st = db_.prepare(
        "SELECT order_id, test_name, instructions FROM lab_order WHERE consultation_id = ? ORDER BY order_id");
    st.bind(1, r.consultationId);
    while (st.step()) r.labOrders.push_back(LabOrderLine{st.getInt64(0), st.getText(1), st.getOptionalText(2)});
}

std::optional<ConsultationRecord> ConsultationRepository::findById(std::int64_t id) {
    std::optional<ConsultationRecord> out;
    {
        auto st = db_.prepare(std::string(kSelect) + "WHERE c.consultation_id = ?");
        st.bind(1, id);
        if (st.step()) out = readRecord(st);
    }
    if (out) loadLines(*out);
    return out;
}

std::vector<ConsultationRecord> ConsultationRepository::listForPatient(std::int64_t uhid) {
    std::vector<ConsultationRecord> out;
    {
        auto st = db_.prepare(std::string(kSelect) + "WHERE c.uhid = ? ORDER BY c.consultation_id DESC");
        st.bind(1, uhid);
        while (st.step()) out.push_back(readRecord(st));
    }
    for (auto& r : out) loadLines(r);
    return out;
}

}  // namespace hms
