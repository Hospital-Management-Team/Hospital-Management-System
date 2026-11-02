#include "record/medical_record_repository.hpp"

namespace hms {

std::string formatHistoryVersionId(std::int64_t id) {
    std::string digits = std::to_string(id);
    if (digits.size() < 6) digits.insert(0, 6 - digits.size(), '0');
    return "MHV-" + digits;
}

std::optional<std::string> MedicalRecordRepository::patientName(std::int64_t uhid) {
    auto st = db_.prepare("SELECT full_name FROM patient WHERE uhid = ?");
    st.bind(1, uhid);
    if (st.step()) return st.getText(0);
    return std::nullopt;
}

std::int64_t MedicalRecordRepository::insert(std::int64_t uhid, const MedicalHistoryDraft& valid,
                                             std::int64_t authorStaffId, const std::string& nowUtc) {
    auto st = db_.prepare(
        "INSERT INTO medical_history_version (uhid, history, allergies, current_medications, author_staff_id, "
        "recorded_at) VALUES (?, ?, ?, ?, ?, ?)");
    st.bind(1, uhid).bind(2, valid.history).bind(3, valid.allergies).bind(4, valid.currentMedications)
        .bind(5, authorStaffId).bind(6, nowUtc);
    st.run();
    return db_.lastInsertRowId();
}

std::vector<MedicalHistoryVersion> MedicalRecordRepository::listForPatient(std::int64_t uhid) {
    auto st = db_.prepare(
        "SELECT v.version_id, v.uhid, v.history, v.allergies, v.current_medications, v.author_staff_id, s.username, "
        "       v.recorded_at "
        "FROM medical_history_version v JOIN staff_accounts s ON s.staff_id = v.author_staff_id "
        "WHERE v.uhid = ? ORDER BY v.version_id DESC");
    st.bind(1, uhid);
    std::vector<MedicalHistoryVersion> out;
    while (st.step()) {
        MedicalHistoryVersion v;
        v.versionId = st.getInt64(0);
        v.uhid = st.getInt64(1);
        v.history = st.getText(2);
        v.allergies = st.getText(3);
        v.currentMedications = st.getText(4);
        v.authorStaffId = st.getInt64(5);
        v.authorUsername = st.getText(6);
        v.recordedAt = st.getText(7);
        out.push_back(std::move(v));
    }
    return out;
}

}  // namespace hms
