#include "admin/admin_repository.hpp"

namespace hms {

namespace {
std::string roleLabel(const std::string& stored) {
    if (stored == "administrator") return "Administrator";
    if (stored == "doctor") return "Doctor";
    if (stored == "receptionist") return "Receptionist";
    if (stored == "lab_technician") return "Lab Technician";
    if (stored == "pharmacist") return "Pharmacist";
    return stored;
}

StaffMember readStaff(const Statement& st) {
    StaffMember m;
    m.staffId = st.getInt64(0);
    m.username = st.getText(1);
    m.role = roleLabel(st.getText(2));
    m.active = st.getInt64(3) != 0;
    m.locked = st.getInt64(4) != 0;
    return m;
}

const char* kStaffColumns =
    "SELECT staff_id, username, role, active, "
    "CASE WHEN locked_until IS NOT NULL AND locked_until > ? THEN 1 ELSE 0 END FROM staff_accounts ";
}  // namespace

std::string AdminRepository::shift() const {
    return (offset_ >= 0 ? "+" : "-") + std::to_string(offset_ >= 0 ? offset_ : -offset_) + " minutes";
}

std::vector<StaffMember> AdminRepository::listStaff(const std::string& nowUtc) {
    std::vector<StaffMember> out;
    auto st = db_.prepare(std::string(kStaffColumns) + "ORDER BY username COLLATE NOCASE");
    st.bind(1, nowUtc);
    while (st.step()) out.push_back(readStaff(st));
    return out;
}

std::optional<StaffMember> AdminRepository::findStaff(std::int64_t staffId, const std::string& nowUtc) {
    auto st = db_.prepare(std::string(kStaffColumns) + "WHERE staff_id = ?");
    st.bind(1, nowUtc).bind(2, staffId);
    if (!st.step()) return std::nullopt;
    return readStaff(st);
}

std::int64_t AdminRepository::activeAdministrators() {
    return db_.queryInt("SELECT COUNT(*) FROM staff_accounts WHERE role = 'administrator' AND active = 1").value_or(0);
}

void AdminRepository::setActive(std::int64_t staffId, bool active) {
    auto st = db_.prepare("UPDATE staff_accounts SET active = ? WHERE staff_id = ?");
    st.bind(1, active ? 1 : 0).bind(2, staffId).run();
}

void AdminRepository::unlock(std::int64_t staffId) {
    auto st = db_.prepare(
        "UPDATE staff_accounts SET failed_attempts = 0, locked = 0, locked_until = NULL WHERE staff_id = ?");
    st.bind(1, staffId).run();
}

void AdminRepository::setPasswordHash(std::int64_t staffId, const std::string& hash) {
    auto st = db_.prepare(
        "UPDATE staff_accounts SET password_hash = ?, failed_attempts = 0, locked = 0, locked_until = NULL "
        "WHERE staff_id = ?");
    st.bind(1, hash).bind(2, staffId).run();
}

std::vector<Department> AdminRepository::listDepartments() {
    std::vector<Department> out;
    auto st = db_.prepare(
        "SELECT d.department_id, d.name, d.active, "
        "(SELECT COUNT(*) FROM doctor_profile p WHERE p.department = d.name COLLATE NOCASE) "
        "FROM department d ORDER BY d.name COLLATE NOCASE");
    while (st.step()) {
        Department d;
        d.departmentId = st.getInt64(0);
        d.name = st.getText(1);
        d.active = st.getInt64(2) != 0;
        d.doctors = st.getInt64(3);
        out.push_back(d);
    }
    return out;
}

std::optional<Department> AdminRepository::findDepartment(std::int64_t id) {
    for (const auto& d : listDepartments()) if (d.departmentId == id) return d;
    return std::nullopt;
}

std::int64_t AdminRepository::addDepartment(const std::string& name) {
    auto st = db_.prepare("INSERT INTO department (name) VALUES (?)");
    st.bind(1, name).run();
    return db_.lastInsertRowId();
}

void AdminRepository::renameDepartment(std::int64_t id, const std::string& oldName, const std::string& newName) {
    auto up = db_.prepare("UPDATE department SET name = ? WHERE department_id = ?");
    up.bind(1, newName).bind(2, id).run();
    // Booking and billing read the department from the doctor profile, so it changes there too.
    auto docs = db_.prepare("UPDATE doctor_profile SET department = ? WHERE department = ? COLLATE NOCASE");
    docs.bind(1, newName).bind(2, oldName).run();
}

void AdminRepository::setDepartmentActive(std::int64_t id, bool active) {
    auto st = db_.prepare("UPDATE department SET active = ? WHERE department_id = ?");
    st.bind(1, active ? 1 : 0).bind(2, id).run();
}

namespace {
ConsultationRoom readRoom(const Statement& st) {
    ConsultationRoom r;
    r.roomId = st.getInt64(0);
    r.name = st.getText(1);
    r.department = st.getOptionalText(2).value_or("");
    r.active = st.getInt64(3) != 0;
    return r;
}
const char* kRoomSelect =
    "SELECT r.room_id, r.name, d.name, r.active FROM consultation_room r "
    "LEFT JOIN department d ON d.department_id = r.department_id ";
}  // namespace

std::vector<ConsultationRoom> AdminRepository::listRooms() {
    std::vector<ConsultationRoom> out;
    auto st = db_.prepare(std::string(kRoomSelect) + "ORDER BY r.name COLLATE NOCASE");
    while (st.step()) out.push_back(readRoom(st));
    return out;
}

std::optional<ConsultationRoom> AdminRepository::findRoom(std::int64_t id) {
    auto st = db_.prepare(std::string(kRoomSelect) + "WHERE r.room_id = ?");
    st.bind(1, id);
    if (!st.step()) return std::nullopt;
    return readRoom(st);
}

std::int64_t AdminRepository::addRoom(const std::string& name, std::optional<std::int64_t> departmentId) {
    auto st = db_.prepare("INSERT INTO consultation_room (name, department_id) VALUES (?, ?)");
    st.bind(1, name).bind(2, departmentId).run();
    return db_.lastInsertRowId();
}

void AdminRepository::updateRoom(std::int64_t id, const std::string& name, std::optional<std::int64_t> departmentId) {
    auto st = db_.prepare("UPDATE consultation_room SET name = ?, department_id = ? WHERE room_id = ?");
    st.bind(1, name).bind(2, departmentId).bind(3, id).run();
}

void AdminRepository::setRoomActive(std::int64_t id, bool active) {
    auto st = db_.prepare("UPDATE consultation_room SET active = ? WHERE room_id = ?");
    st.bind(1, active ? 1 : 0).bind(2, id).run();
}

std::vector<DoctorMapping> AdminRepository::listDoctorMappings() {
    std::vector<DoctorMapping> out;
    auto st = db_.prepare(
        "SELECT s.staff_id, s.username, p.department FROM staff_accounts s "
        "JOIN doctor_profile p ON p.staff_id = s.staff_id WHERE s.role = 'doctor' AND s.active = 1 "
        "ORDER BY s.username COLLATE NOCASE");
    while (st.step()) out.push_back({st.getInt64(0), st.getText(1), st.getText(2)});
    return out;
}

bool AdminRepository::hasDoctorProfile(std::int64_t staffId) {
    auto st = db_.prepare("SELECT 1 FROM doctor_profile WHERE staff_id = ?");
    st.bind(1, staffId);
    return st.step();
}

void AdminRepository::setDoctorDepartment(std::int64_t staffId, const std::string& name, const std::string& nowUtc) {
    auto st = db_.prepare("UPDATE doctor_profile SET department = ?, updated_at = ? WHERE staff_id = ?");
    st.bind(1, name).bind(2, nowUtc).bind(3, staffId).run();
}

HospitalReport AdminRepository::report(const std::string& from, const std::string& to) {
    HospitalReport r;
    r.from = from;
    r.to = to;
    const std::string mod = shift();

    r.registeredPatients = db_.queryInt("SELECT COUNT(*) FROM patient").value_or(0);

    {   // Appointments by slot date (slot_start is already hospital-local).
        auto st = db_.prepare(
            "SELECT COUNT(*), COALESCE(SUM(status = 'BOOKED'), 0), COALESCE(SUM(status = 'COMPLETED'), 0), "
            "COALESCE(SUM(status = 'CANCELLED'), 0), COALESCE(SUM(status = 'NO_SHOW'), 0), "
            "COUNT(DISTINCT CASE WHEN status IN ('BOOKED', 'COMPLETED') THEN uhid END) "
            "FROM appointment WHERE substr(slot_start, 1, 10) BETWEEN ? AND ?");
        st.bind(1, from).bind(2, to);
        if (st.step()) {
            r.appointments = st.getInt64(0);
            r.booked = st.getInt64(1);
            r.completed = st.getInt64(2);
            r.cancelled = st.getInt64(3);
            r.noShow = st.getInt64(4);
            r.patientsSeen = st.getInt64(5);
        }
    }
    {
        auto st = db_.prepare(
            "SELECT substr(slot_start, 1, 10) AS d, "
            "COUNT(DISTINCT CASE WHEN status IN ('BOOKED', 'COMPLETED') THEN uhid END), "
            "SUM(status = 'BOOKED'), SUM(status = 'COMPLETED'), SUM(status = 'CANCELLED'), SUM(status = 'NO_SHOW') "
            "FROM appointment WHERE d BETWEEN ? AND ? GROUP BY d ORDER BY d");
        st.bind(1, from).bind(2, to);
        while (st.step()) {
            DayActivity d;
            d.date = st.getText(0);
            d.patientsSeen = st.getInt64(1);
            d.booked = st.getInt64(2);
            d.completed = st.getInt64(3);
            d.cancelled = st.getInt64(4);
            d.noShow = st.getInt64(5);
            r.days.push_back(d);
        }
    }
    {
        auto st = db_.prepare(
            "SELECT s.username, p.department, SUM(a.status = 'BOOKED'), SUM(a.status = 'COMPLETED'), "
            "SUM(a.status = 'CANCELLED'), SUM(a.status = 'NO_SHOW') "
            "FROM appointment a JOIN staff_accounts s ON s.staff_id = a.doctor_staff_id "
            "JOIN doctor_profile p ON p.staff_id = a.doctor_staff_id "
            "WHERE substr(a.slot_start, 1, 10) BETWEEN ? AND ? "
            "GROUP BY a.doctor_staff_id ORDER BY s.username COLLATE NOCASE");
        st.bind(1, from).bind(2, to);
        while (st.step()) {
            DoctorActivity d;
            d.doctor = st.getText(0);
            d.department = st.getText(1);
            d.booked = st.getInt64(2);
            d.completed = st.getInt64(3);
            d.cancelled = st.getInt64(4);
            d.noShow = st.getInt64(5);
            r.doctors.push_back(d);
        }
    }
    {   // Consultations by the local date they were recorded.
        auto st = db_.prepare("SELECT COUNT(*) FROM consultation WHERE date(recorded_at, ?) BETWEEN ? AND ?");
        st.bind(1, mod).bind(2, from).bind(3, to);
        if (st.step()) r.consultations = st.getInt64(0);
    }
    {   // Bills by the local date they were created; outstanding = billed minus everything paid on them.
        auto st = db_.prepare(
            "SELECT COUNT(*), COALESCE(SUM(b.total_paise), 0), "
            "COALESCE(SUM(b.total_paise - (SELECT COALESCE(SUM(p.amount_paise), 0) FROM payment p "
            "WHERE p.bill_id = b.bill_id)), 0) FROM bill b WHERE date(b.created_at, ?) BETWEEN ? AND ?");
        st.bind(1, mod).bind(2, from).bind(3, to);
        if (st.step()) {
            r.billsCount = st.getInt64(0);
            r.billedPaise = st.getInt64(1);
            r.outstandingPaise = st.getInt64(2);
        }
    }
    {   // Revenue: payments received on the local date, by mode.
        auto st = db_.prepare(
            "SELECT mode, COUNT(*), SUM(amount_paise) FROM payment WHERE date(paid_at, ?) BETWEEN ? AND ? "
            "GROUP BY mode ORDER BY mode");
        st.bind(1, mod).bind(2, from).bind(3, to);
        while (st.step()) {
            ModeTotal m;
            m.mode = st.getText(0);
            m.paise = st.getInt64(2);
            r.paymentsCount += st.getInt64(1);
            r.collectedPaise += m.paise;
            r.byMode.push_back(m);
        }
    }
    {
        auto st = db_.prepare(
            "SELECT role, SUM(active = 1), SUM(active = 0) FROM staff_accounts GROUP BY role ORDER BY role");
        while (st.step()) {
            RoleCount c;
            c.role = roleLabel(st.getText(0));
            c.active = st.getInt64(1);
            c.deactivated = st.getInt64(2);
            r.staff.push_back(c);
        }
    }
    return r;
}

}  // namespace hms
