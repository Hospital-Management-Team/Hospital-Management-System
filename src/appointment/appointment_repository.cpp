#include "appointment/appointment_repository.hpp"

namespace hms {

namespace {

// Columns read by readRecord(), in this order.
const char* kSelectRecord =
    "SELECT a.appointment_id, a.uhid, p.full_name, a.doctor_staff_id, s.username, d.department, "
    "       a.slot_start, a.slot_minutes, a.status, a.replaced_by "
    "FROM appointment a "
    "JOIN patient p ON p.uhid = a.uhid "
    "JOIN staff_accounts s ON s.staff_id = a.doctor_staff_id "
    "JOIN doctor_profile d ON d.staff_id = a.doctor_staff_id ";

AppointmentRecord readRecord(const Statement& st) {
    AppointmentRecord r;
    r.id = st.getInt64(0);
    r.uhid = st.getInt64(1);
    r.patientName = st.getText(2);
    r.doctorStaffId = st.getInt64(3);
    r.doctorUsername = st.getText(4);
    r.department = st.getText(5);
    if (auto s = LocalSlot::fromKey(st.getText(6))) r.start = *s;
    r.slotMinutes = static_cast<int>(st.getInt64(7));
    r.status = statusFromStorage(st.getText(8)).value_or(AppointmentStatus::Booked);
    r.replacedBy = st.getOptionalInt64(9);
    return r;
}

std::vector<AppointmentRecord> collect(Statement& st) {
    std::vector<AppointmentRecord> out;
    while (st.step()) out.push_back(readRecord(st));
    return out;
}

// "YYYY-MM-DD" -> every slot key on that date starts with "YYYY-MM-DDT".
std::string dayPrefixPattern(const Date& d) { return d.toIso() + "T%"; }

}  // namespace

std::vector<WeeklyHours> AppointmentRepository::hoursFor(std::int64_t staffId) {
    auto st = db_.prepare(
        "SELECT weekday, start_minute, end_minute FROM doctor_availability WHERE staff_id = ? ORDER BY weekday");
    st.bind(1, staffId);
    std::vector<WeeklyHours> out;
    while (st.step()) {
        out.push_back(WeeklyHours{static_cast<int>(st.getInt64(0)), static_cast<int>(st.getInt64(1)),
                                  static_cast<int>(st.getInt64(2))});
    }
    return out;
}

std::vector<DoctorSummary> AppointmentRepository::listDoctors() {
    auto st = db_.prepare(
        "SELECT s.staff_id, s.username, d.department, d.specialization "
        "FROM staff_accounts s LEFT JOIN doctor_profile d ON d.staff_id = s.staff_id "
        "WHERE s.role = 'doctor' ORDER BY s.username COLLATE NOCASE");
    std::vector<DoctorSummary> out;
    while (st.step()) {
        DoctorSummary d;
        d.staffId = st.getInt64(0);
        d.username = st.getText(1);
        d.hasProfile = !st.isNull(2);
        if (d.hasProfile) d.department = st.getText(2);
        d.specialization = st.getOptionalText(3);
        out.push_back(std::move(d));
    }
    for (auto& d : out) d.hours = hoursFor(d.staffId);
    return out;
}

std::optional<DoctorSummary> AppointmentRepository::findDoctor(std::int64_t staffId) {
    for (auto& d : listDoctors())
        if (d.staffId == staffId) return d;
    return std::nullopt;
}

std::optional<std::int64_t> AppointmentRepository::doctorIdForUsername(const std::string& username) {
    auto st = db_.prepare("SELECT staff_id FROM staff_accounts WHERE username = ? AND role = 'doctor'");
    st.bind(1, username);
    if (st.step()) return st.getInt64(0);
    return std::nullopt;
}

void AppointmentRepository::saveSchedule(std::int64_t staffId, const DoctorScheduleDraft& draft,
                                         const std::string& updatedAtUtc) {
    auto up = db_.prepare(
        "INSERT INTO doctor_profile (staff_id, department, specialization, updated_at) VALUES (?, ?, ?, ?) "
        "ON CONFLICT (staff_id) DO UPDATE SET department = excluded.department, "
        "specialization = excluded.specialization, updated_at = excluded.updated_at");
    up.bind(1, staffId).bind(2, draft.department);
    if (draft.specialization.empty()) up.bindNull(3);
    else up.bind(3, draft.specialization);
    up.bind(4, updatedAtUtc);
    up.run();

    auto del = db_.prepare("DELETE FROM doctor_availability WHERE staff_id = ?");
    del.bind(1, staffId);
    del.run();

    auto ins = db_.prepare(
        "INSERT INTO doctor_availability (staff_id, weekday, start_minute, end_minute) VALUES (?, ?, ?, ?)");
    for (const auto& h : draft.hours) {
        ins.reset();
        ins.bind(1, staffId).bind(2, h.weekday).bind(3, h.startMinute).bind(4, h.endMinute);
        ins.run();
    }
}

bool AppointmentRepository::patientExists(std::int64_t uhid) {
    auto st = db_.prepare("SELECT 1 FROM patient WHERE uhid = ?");
    st.bind(1, uhid);
    return st.step();
}

std::optional<AppointmentRecord> AppointmentRepository::findAppointment(std::int64_t id) {
    auto st = db_.prepare(std::string(kSelectRecord) + "WHERE a.appointment_id = ?");
    st.bind(1, id);
    if (st.step()) return readRecord(st);
    return std::nullopt;
}

std::vector<AppointmentRecord> AppointmentRepository::occupyingForDoctorOn(std::int64_t staffId, const Date& date) {
    auto st = db_.prepare(std::string(kSelectRecord) +
                          "WHERE a.doctor_staff_id = ? AND a.status IN ('BOOKED', 'COMPLETED') "
                          "AND a.slot_start LIKE ? ORDER BY a.slot_start");
    st.bind(1, staffId).bind(2, dayPrefixPattern(date));
    return collect(st);
}

std::vector<AppointmentRecord> AppointmentRepository::bookedForPatientOn(std::int64_t uhid, const Date& date) {
    auto st = db_.prepare(std::string(kSelectRecord) +
                          "WHERE a.uhid = ? AND a.status = 'BOOKED' AND a.slot_start LIKE ? ORDER BY a.slot_start");
    st.bind(1, uhid).bind(2, dayPrefixPattern(date));
    return collect(st);
}

std::vector<AppointmentRecord> AppointmentRepository::bookedForDoctorFrom(std::int64_t staffId,
                                                                          const std::string& fromKey) {
    auto st = db_.prepare(std::string(kSelectRecord) +
                          "WHERE a.doctor_staff_id = ? AND a.status = 'BOOKED' AND a.slot_start >= ? "
                          "ORDER BY a.slot_start");
    st.bind(1, staffId).bind(2, fromKey);
    return collect(st);
}

std::vector<AppointmentRecord> AppointmentRepository::bookedForPatientFrom(std::int64_t uhid,
                                                                           const std::string& fromKey) {
    auto st = db_.prepare(std::string(kSelectRecord) +
                          "WHERE a.uhid = ? AND a.status = 'BOOKED' AND a.slot_start >= ? ORDER BY a.slot_start");
    st.bind(1, uhid).bind(2, fromKey);
    return collect(st);
}

std::vector<AppointmentRecord> AppointmentRepository::queueFor(std::int64_t staffId, const Date& date) {
    auto st = db_.prepare(std::string(kSelectRecord) +
                          "WHERE a.doctor_staff_id = ? AND a.status <> 'CANCELLED' AND a.slot_start LIKE ? "
                          "ORDER BY a.slot_start");
    st.bind(1, staffId).bind(2, dayPrefixPattern(date));
    return collect(st);
}

std::vector<std::pair<AppointmentRecord, bool>> AppointmentRepository::bookedBetween(const std::string& fromKey,
                                                                                     const std::string& toKey) {
    auto st = db_.prepare(
        "SELECT a.appointment_id, (SELECT count(*) FROM appointment_reminder r "
        "                          WHERE r.appointment_id = a.appointment_id) "
        "FROM appointment a WHERE a.status = 'BOOKED' AND a.slot_start >= ? AND a.slot_start <= ? "
        "ORDER BY a.slot_start, a.appointment_id");
    st.bind(1, fromKey).bind(2, toKey);
    std::vector<std::pair<std::int64_t, bool>> ids;
    while (st.step()) ids.emplace_back(st.getInt64(0), st.getInt64(1) > 0);
    std::vector<std::pair<AppointmentRecord, bool>> out;
    for (const auto& [id, reminded] : ids) {
        if (auto r = findAppointment(id)) out.emplace_back(std::move(*r), reminded);
    }
    return out;
}

std::int64_t AppointmentRepository::insertAppointment(std::int64_t uhid, std::int64_t staffId, const LocalSlot& start,
                                                      int slotMinutes, const std::string& createdAtUtc,
                                                      const std::string& createdBy) {
    auto st = db_.prepare(
        "INSERT INTO appointment (uhid, doctor_staff_id, slot_start, slot_minutes, status, created_at, created_by) "
        "VALUES (?, ?, ?, ?, 'BOOKED', ?, ?)");
    st.bind(1, uhid).bind(2, staffId).bind(3, start.toKey()).bind(4, slotMinutes).bind(5, createdAtUtc).bind(6, createdBy);
    st.run();
    return db_.lastInsertRowId();
}

void AppointmentRepository::markCompleted(std::int64_t id) {
    auto st = db_.prepare("UPDATE appointment SET status = 'COMPLETED' WHERE appointment_id = ? AND status = 'BOOKED'");
    st.bind(1, id);
    st.run();
}

void AppointmentRepository::markCancelled(std::int64_t id, const std::string& atUtc, const std::string& by) {
    auto st = db_.prepare(
        "UPDATE appointment SET status = 'CANCELLED', cancelled_at = ?, cancelled_by = ? "
        "WHERE appointment_id = ? AND status = 'BOOKED'");
    st.bind(1, atUtc).bind(2, by).bind(3, id);
    st.run();
}

void AppointmentRepository::setReplacedBy(std::int64_t id, std::int64_t replacementId) {
    auto st = db_.prepare("UPDATE appointment SET replaced_by = ? WHERE appointment_id = ?");
    st.bind(1, replacementId).bind(2, id);
    st.run();
}

void AppointmentRepository::insertReminder(std::int64_t id, const std::string& atUtc, const std::string& by) {
    auto st = db_.prepare(
        "INSERT INTO appointment_reminder (appointment_id, generated_at, generated_by) VALUES (?, ?, ?)");
    st.bind(1, id).bind(2, atUtc).bind(3, by);
    st.run();
}

}  // namespace hms
