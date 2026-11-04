#pragma once
// SQL for administration and reports (issue #9). Holds no business rules; reports only read.

#include <optional>
#include <string>
#include <vector>

#include "admin/admin.hpp"
#include "data/database.hpp"

namespace hms {

class AdminRepository {
public:
    AdminRepository(Database& db, int utcOffsetMinutes) : db_(db), offset_(utcOffsetMinutes) {}

    std::vector<StaffMember> listStaff(const std::string& nowUtc);
    std::optional<StaffMember> findStaff(std::int64_t staffId, const std::string& nowUtc);
    std::int64_t activeAdministrators();
    void setActive(std::int64_t staffId, bool active);
    void unlock(std::int64_t staffId);
    void setPasswordHash(std::int64_t staffId, const std::string& hash);

    // Master data (HMS-F-050)
    std::vector<Department> listDepartments();
    std::optional<Department> findDepartment(std::int64_t id);
    std::int64_t addDepartment(const std::string& name);     // throws DbError on a duplicate name
    void renameDepartment(std::int64_t id, const std::string& oldName, const std::string& newName);
    void setDepartmentActive(std::int64_t id, bool active);
    std::vector<ConsultationRoom> listRooms();
    std::optional<ConsultationRoom> findRoom(std::int64_t id);
    std::int64_t addRoom(const std::string& name, std::optional<std::int64_t> departmentId);
    void updateRoom(std::int64_t id, const std::string& name, std::optional<std::int64_t> departmentId);
    void setRoomActive(std::int64_t id, bool active);
    std::vector<DoctorMapping> listDoctorMappings();
    bool hasDoctorProfile(std::int64_t staffId);
    void setDoctorDepartment(std::int64_t staffId, const std::string& departmentName, const std::string& nowUtc);

    // Fills every field of the report for the inclusive local date range.
    HospitalReport report(const std::string& fromIso, const std::string& toIso);

private:
    std::string shift() const;  // SQLite modifier that turns a UTC time into hospital-local time
    Database& db_;
    int offset_;
};

}  // namespace hms
