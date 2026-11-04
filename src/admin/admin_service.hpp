#pragma once
// Administration and reporting (SRS HMS-F-023, HMS-F-050, HMS-F-051, HMS-SR-001, HMS-SR-004; issue #9).
// Follows the module contract: the role is checked first (a denial is audited), input is
// validated, and each change is made in one transaction together with its audit entry.
//
//  - Staff accounts (Admin): list, deactivate, reactivate, unlock, reset a password.
//    A deactivated account cannot log in (login refuses it). The Admin cannot deactivate their
//    own account, and the last active Administrator cannot be deactivated.
//  - Reports (Admin, read only): totals for a date range, read from the same tables the other
//    modules write, so the figures always match the transactional data.
//  - Audit entries hold IDs and field names only, never passwords or amounts.

#include <string>
#include <vector>

#include "admin/admin.hpp"
#include "admin/admin_repository.hpp"
#include "audit/audit_log.hpp"
#include "auth/authorizer.hpp"
#include "auth/session.hpp"
#include "config/policy.hpp"
#include "core/clock.hpp"
#include "core/result.hpp"
#include "data/database.hpp"

namespace hms {

class AdminService {
public:
    AdminService(Database& db, const Clock& clock, AppPolicy policy);

    Result<std::vector<StaffMember>> listStaff(const Session& session);
    Result<StaffMember> deactivate(const Session& session, std::int64_t staffId);
    Result<StaffMember> reactivate(const Session& session, std::int64_t staffId);
    Result<StaffMember> unlock(const Session& session, std::int64_t staffId);
    Result<StaffMember> resetPassword(const Session& session, std::int64_t staffId, const std::string& newPassword);

    // Master data (HMS-F-050, Admin). Names are 1 to policy.masterData.maxNameChars characters and
    // unique ignoring case. A department with doctors in it cannot be deactivated. Renaming a
    // department changes it on every doctor profile in it, so booking and billing show it at once.
    Result<std::vector<Department>> listDepartments(const Session& session);
    Result<Department> addDepartment(const Session& session, const std::string& name);
    Result<Department> renameDepartment(const Session& session, std::int64_t id, const std::string& newName);
    Result<Department> setDepartmentActive(const Session& session, std::int64_t id, bool active);
    Result<std::vector<ConsultationRoom>> listRooms(const Session& session);
    // departmentId 0 means "not tied to a department".
    Result<ConsultationRoom> addRoom(const Session& session, const std::string& name, std::int64_t departmentId);
    Result<ConsultationRoom> updateRoom(const Session& session, std::int64_t id, const std::string& name,
                                        std::int64_t departmentId);
    Result<ConsultationRoom> setRoomActive(const Session& session, std::int64_t id, bool active);
    Result<std::vector<DoctorMapping>> listDoctorMappings(const Session& session);
    // Puts a doctor (who already has a profile) in an active department.
    Result<DoctorMapping> assignDoctorDepartment(const Session& session, std::int64_t doctorStaffId,
                                                 std::int64_t departmentId);

    // Inclusive hospital-local dates; from must not be after to, and the range is limited.
    Result<HospitalReport> report(const Session& session, const Date& from, const Date& to);

    const AppPolicy& policy() const { return policy_; }

private:
    bool authorize(const Session& session, Permission permission);
    Result<std::string> cleanName(const std::string& raw, const std::string& field) const;
    Result<StaffMember> setActive(const Session& session, std::int64_t staffId, bool active);

    Database& db_;
    const Clock& clock_;
    AppPolicy policy_;
    AuditLog audit_;
    AdminRepository repo_;
};

}  // namespace hms
