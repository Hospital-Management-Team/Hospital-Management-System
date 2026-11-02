#include "lab/lab_repository.hpp"

#include <functional>

namespace hms {

namespace {
// One row per ordered test, with the latest revision (if any) of its result.
const char* kOrderSelect =
    "SELECT o.order_id, o.consultation_id, c.uhid, p.full_name, d.username, o.test_name, o.instructions, "
    "o.ordered_at, "
    "(SELECT COUNT(*) FROM lab_result r WHERE r.order_id = o.order_id), "
    "lr.revision, lr.result_text, lr.entered_by, lr.entered_at "
    "FROM lab_order o "
    "JOIN consultation c ON c.consultation_id = o.consultation_id "
    "JOIN patient p ON p.uhid = c.uhid "
    "JOIN staff_accounts d ON d.staff_id = c.doctor_staff_id "
    "LEFT JOIN lab_result lr ON lr.order_id = o.order_id AND lr.revision = "
    "(SELECT MAX(r2.revision) FROM lab_result r2 WHERE r2.order_id = o.order_id) ";

LabOrderView readOrder(const Statement& st) {
    LabOrderView v;
    v.orderId = st.getInt64(0);
    v.consultationId = st.getInt64(1);
    v.uhid = st.getInt64(2);
    v.patientName = st.getText(3);
    v.doctorUsername = st.getText(4);
    v.testName = st.getText(5);
    v.instructions = st.getOptionalText(6);
    v.orderedAt = st.getText(7);
    v.revisionCount = static_cast<int>(st.getInt64(8));
    if (v.revisionCount > 0) {
        LabResultRevision r;
        r.revision = static_cast<int>(st.getInt64(9));
        r.text = st.getText(10);
        r.enteredBy = st.getText(11);
        r.enteredAt = st.getText(12);
        v.latest = r;
    }
    return v;
}
}  // namespace

std::vector<LabOrderView> LabRepository::query(const std::string& where, const std::string& tail,
                                               const std::function<void(Statement&)>& bind) {
    Statement st = db_.prepare(std::string(kOrderSelect) + where + " " + tail);
    bind(st);
    std::vector<LabOrderView> out;
    while (st.step()) out.push_back(readOrder(st));
    return out;
}

std::optional<LabOrderView> LabRepository::findOrder(std::int64_t orderId) {
    auto rows = query("WHERE o.order_id = ?1", "", [&](Statement& st) { st.bind(1, orderId); });
    if (rows.empty()) return std::nullopt;
    return rows.front();
}

std::vector<LabOrderView> LabRepository::listPending(std::size_t limit) {
    return query("WHERE NOT EXISTS (SELECT 1 FROM lab_result r WHERE r.order_id = o.order_id)",
                 "ORDER BY o.ordered_at, o.order_id LIMIT ?1",
                 [&](Statement& st) { st.bind(1, static_cast<std::int64_t>(limit)); });
}

std::vector<LabOrderView> LabRepository::listForPatient(std::int64_t uhid) {
    return query("WHERE c.uhid = ?1", "ORDER BY o.order_id DESC", [&](Statement& st) { st.bind(1, uhid); });
}

std::vector<LabOrderView> LabRepository::listForPatientByDoctor(std::int64_t uhid, std::int64_t doctorStaffId) {
    return query("WHERE c.uhid = ?1 AND c.doctor_staff_id = ?2", "ORDER BY o.order_id DESC", [&](Statement& st) {
        st.bind(1, uhid);
        st.bind(2, doctorStaffId);
    });
}

bool LabRepository::patientExists(std::int64_t uhid) {
    Statement st = db_.prepare("SELECT 1 FROM patient WHERE uhid = ?1");
    st.bind(1, uhid);
    return st.step();
}

std::vector<LabResultRevision> LabRepository::history(std::int64_t orderId) {
    Statement st = db_.prepare(
        "SELECT revision, result_text, entered_by, entered_at FROM lab_result WHERE order_id = ?1 ORDER BY revision");
    st.bind(1, orderId);
    std::vector<LabResultRevision> out;
    while (st.step()) {
        out.push_back(LabResultRevision{static_cast<int>(st.getInt64(0)), st.getText(1), st.getText(2), st.getText(3)});
    }
    return out;
}

std::optional<LabResultRevision> LabRepository::latest(std::int64_t orderId) {
    auto all = history(orderId);
    if (all.empty()) return std::nullopt;
    return all.back();
}

void LabRepository::insertRevision(std::int64_t orderId, int revision, const std::string& text,
                                   const std::string& enteredBy, const std::string& nowUtc) {
    Statement st = db_.prepare(
        "INSERT INTO lab_result (order_id, revision, result_text, entered_by, entered_at) VALUES (?1, ?2, ?3, ?4, ?5)");
    st.bind(1, orderId);
    st.bind(2, revision);
    st.bind(3, text);
    st.bind(4, enteredBy);
    st.bind(5, nowUtc);
    st.step();
}

}  // namespace hms
