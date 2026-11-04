#include "admin/admin_service.hpp"

#include "auth/authentication.hpp"
#include "core/text.hpp"
#include "validation/input.hpp"

namespace hms {

namespace {
const char* kStorageFailed = "The data store reported an error. Nothing was saved. Please try again.";
const char* kReadFailed = "The data store reported an error. Please try again.";
const char* kOutcomeUnknown =
    "The data store could not confirm whether the change was saved. Do not repeat it. "
    "Restart the application and check the staff list.";

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

Error accountNotFound(std::int64_t id) { return Error::notFound("No staff account found with ID " + std::to_string(id) + "."); }
}  // namespace

AdminService::AdminService(Database& db, const Clock& clock, AppPolicy policy)
    : db_(db), clock_(clock), policy_(policy), audit_(db, clock), repo_(db, policy.utcOffsetMinutes) {}

bool AdminService::authorize(const Session& session, Permission permission) {
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

Result<std::vector<StaffMember>> AdminService::listStaff(const Session& session) {
    if (!authorize(session, Permission::ManageStaffAccounts)) {
        return Error::accessDenied("Your role (" + to_string(session.role) + ") may not manage staff accounts.");
    }
    return guarded<std::vector<StaffMember>>(false, [&]() -> Result<std::vector<StaffMember>> {
        return repo_.listStaff(toIsoUtc(clock_.now()));
    });
}

Result<StaffMember> AdminService::setActive(const Session& session, std::int64_t staffId, bool active) {
    if (!authorize(session, Permission::ManageStaffAccounts)) {
        return Error::accessDenied("Your role (" + to_string(session.role) + ") may not manage staff accounts.");
    }
    return guarded<StaffMember>(true, [&]() -> Result<StaffMember> {
        Transaction tx(db_);
        const std::string now = toIsoUtc(clock_.now());
        auto found = repo_.findStaff(staffId, now);
        if (!found) return accountNotFound(staffId);
        if (found->active == active) {
            return Error::conflict(active ? "That account is already active." : "That account is already deactivated.");
        }
        if (!active) {
            if (found->username == session.userId) return Error::conflict("You cannot deactivate your own account.");
            if (found->role == "Administrator" && repo_.activeAdministrators() <= 1) {
                return Error::conflict("The last active Administrator account cannot be deactivated.");
            }
        }
        repo_.setActive(staffId, active);
        AuditEntryInput e;
        e.action = active ? audit_action::kStaffAccountReactivate : audit_action::kStaffAccountDeactivate;
        e.recordId = std::to_string(staffId);
        e.changedFields = {"active"};
        audit_.append(session.userId, e);
        db_.faultPoint("before_commit");
        tx.commit();
        found->active = active;
        return *found;
    });
}

Result<StaffMember> AdminService::deactivate(const Session& session, std::int64_t staffId) {
    return setActive(session, staffId, false);
}
Result<StaffMember> AdminService::reactivate(const Session& session, std::int64_t staffId) {
    return setActive(session, staffId, true);
}

Result<StaffMember> AdminService::unlock(const Session& session, std::int64_t staffId) {
    if (!authorize(session, Permission::ManageStaffAccounts)) {
        return Error::accessDenied("Your role (" + to_string(session.role) + ") may not manage staff accounts.");
    }
    return guarded<StaffMember>(true, [&]() -> Result<StaffMember> {
        Transaction tx(db_);
        auto found = repo_.findStaff(staffId, toIsoUtc(clock_.now()));
        if (!found) return accountNotFound(staffId);
        repo_.unlock(staffId);
        AuditEntryInput e;
        e.action = audit_action::kStaffAccountUnlock;
        e.recordId = std::to_string(staffId);
        e.changedFields = {"failed_attempts", "locked_until"};
        audit_.append(session.userId, e);
        db_.faultPoint("before_commit");
        tx.commit();
        found->locked = false;
        return *found;
    });
}

Result<StaffMember> AdminService::resetPassword(const Session& session, std::int64_t staffId,
                                                const std::string& newPassword) {
    if (!authorize(session, Permission::ManageStaffAccounts)) {
        return Error::accessDenied("Your role (" + to_string(session.role) + ") may not manage staff accounts.");
    }
    std::string reason;
    if (!auth::isPasswordAcceptable(newPassword, reason)) return Error::validation("Password", reason);
    const std::string hash = auth::makePasswordHash(newPassword);
    return guarded<StaffMember>(true, [&]() -> Result<StaffMember> {
        Transaction tx(db_);
        auto found = repo_.findStaff(staffId, toIsoUtc(clock_.now()));
        if (!found) return accountNotFound(staffId);
        repo_.setPasswordHash(staffId, hash);
        AuditEntryInput e;
        e.action = audit_action::kStaffPasswordReset;
        e.recordId = std::to_string(staffId);
        e.changedFields = {"password_hash"};  // the name only, never the password
        audit_.append(session.userId, e);
        db_.faultPoint("before_commit");
        tx.commit();
        found->locked = false;
        return *found;
    });
}

Result<std::string> AdminService::cleanName(const std::string& raw, const std::string& field) const {
    auto clean = input::requireLine(raw, field);
    if (!clean) return clean.error();
    if (text::utf8Length(clean.value()) > policy_.masterData.maxNameChars) {
        return Error::validation(field, "must be at most " + std::to_string(policy_.masterData.maxNameChars) + " characters");
    }
    return clean.value();
}

namespace {
bool sameName(const std::string& a, const std::string& b) { return text::asciiLower(a) == text::asciiLower(b); }
const char* kNoMasterData = "may not manage master data.";
Error deniedMaster(const Session& s) { return Error::accessDenied("Your role (" + to_string(s.role) + ") " + kNoMasterData); }
Error duplicate(const char* what) { return Error::conflict(std::string("A ") + what + " with that name already exists."); }
Error departmentMissing(std::int64_t id) { return Error::notFound("No department found with number " + std::to_string(id) + "."); }
Error roomMissing(std::int64_t id) { return Error::notFound("No consultation room found with number " + std::to_string(id) + "."); }
}  // namespace

Result<std::vector<Department>> AdminService::listDepartments(const Session& session) {
    if (!authorize(session, Permission::ManageMasterData)) return deniedMaster(session);
    return guarded<std::vector<Department>>(false, [&]() -> Result<std::vector<Department>> { return repo_.listDepartments(); });
}

Result<Department> AdminService::addDepartment(const Session& session, const std::string& name) {
    if (!authorize(session, Permission::ManageMasterData)) return deniedMaster(session);
    auto clean = cleanName(name, "Department name");
    if (!clean) return clean.error();
    return guarded<Department>(true, [&]() -> Result<Department> {
        Transaction tx(db_);
        for (const auto& d : repo_.listDepartments()) {
            if (sameName(d.name, clean.value())) return duplicate("department");
        }
        const auto id = repo_.addDepartment(clean.value());
        AuditEntryInput e;
        e.action = audit_action::kDepartmentCreate;
        e.recordId = std::to_string(id);
        e.changedFields = {"name"};
        audit_.append(session.userId, e);
        db_.faultPoint("before_commit");
        tx.commit();
        return Department{id, clean.value(), true, 0};
    });
}

Result<Department> AdminService::renameDepartment(const Session& session, std::int64_t id, const std::string& newName) {
    if (!authorize(session, Permission::ManageMasterData)) return deniedMaster(session);
    auto clean = cleanName(newName, "Department name");
    if (!clean) return clean.error();
    return guarded<Department>(true, [&]() -> Result<Department> {
        Transaction tx(db_);
        auto found = repo_.findDepartment(id);
        if (!found) return departmentMissing(id);
        for (const auto& d : repo_.listDepartments()) {
            if (d.departmentId != id && sameName(d.name, clean.value())) return duplicate("department");
        }
        repo_.renameDepartment(id, found->name, clean.value());
        AuditEntryInput e;
        e.action = audit_action::kDepartmentUpdate;
        e.recordId = std::to_string(id);
        e.changedFields = {"name"};
        audit_.append(session.userId, e);
        db_.faultPoint("before_commit");
        tx.commit();
        found->name = clean.value();
        return *found;
    });
}

Result<Department> AdminService::setDepartmentActive(const Session& session, std::int64_t id, bool active) {
    if (!authorize(session, Permission::ManageMasterData)) return deniedMaster(session);
    return guarded<Department>(true, [&]() -> Result<Department> {
        Transaction tx(db_);
        auto found = repo_.findDepartment(id);
        if (!found) return departmentMissing(id);
        if (found->active == active) return Error::conflict(active ? "That department is already active." : "That department is already switched off.");
        if (!active && found->doctors > 0) {
            return Error::conflict("Doctors are still in this department. Move them to another department first.");
        }
        repo_.setDepartmentActive(id, active);
        AuditEntryInput e;
        e.action = audit_action::kDepartmentUpdate;
        e.recordId = std::to_string(id);
        e.changedFields = {"active"};
        audit_.append(session.userId, e);
        db_.faultPoint("before_commit");
        tx.commit();
        found->active = active;
        return *found;
    });
}

Result<std::vector<ConsultationRoom>> AdminService::listRooms(const Session& session) {
    if (!authorize(session, Permission::ManageMasterData)) return deniedMaster(session);
    return guarded<std::vector<ConsultationRoom>>(false, [&]() -> Result<std::vector<ConsultationRoom>> { return repo_.listRooms(); });
}

Result<ConsultationRoom> AdminService::addRoom(const Session& session, const std::string& name, std::int64_t departmentId) {
    if (!authorize(session, Permission::ManageMasterData)) return deniedMaster(session);
    auto clean = cleanName(name, "Room name");
    if (!clean) return clean.error();
    return guarded<ConsultationRoom>(true, [&]() -> Result<ConsultationRoom> {
        Transaction tx(db_);
        std::optional<std::int64_t> dept;
        if (departmentId != 0) {
            auto d = repo_.findDepartment(departmentId);
            if (!d) return departmentMissing(departmentId);
            if (!d->active) return Error::conflict("That department is switched off.");
            dept = departmentId;
        }
        for (const auto& r : repo_.listRooms()) {
            if (sameName(r.name, clean.value())) return duplicate("consultation room");
        }
        const auto id = repo_.addRoom(clean.value(), dept);
        AuditEntryInput e;
        e.action = audit_action::kRoomCreate;
        e.recordId = std::to_string(id);
        e.changedFields = {"name", "department"};
        audit_.append(session.userId, e);
        db_.faultPoint("before_commit");
        tx.commit();
        return *repo_.findRoom(id);
    });
}

Result<ConsultationRoom> AdminService::updateRoom(const Session& session, std::int64_t id, const std::string& name,
                                                  std::int64_t departmentId) {
    if (!authorize(session, Permission::ManageMasterData)) return deniedMaster(session);
    auto clean = cleanName(name, "Room name");
    if (!clean) return clean.error();
    return guarded<ConsultationRoom>(true, [&]() -> Result<ConsultationRoom> {
        Transaction tx(db_);
        if (!repo_.findRoom(id)) return roomMissing(id);
        std::optional<std::int64_t> dept;
        if (departmentId != 0) {
            auto d = repo_.findDepartment(departmentId);
            if (!d) return departmentMissing(departmentId);
            if (!d->active) return Error::conflict("That department is switched off.");
            dept = departmentId;
        }
        for (const auto& r : repo_.listRooms()) {
            if (r.roomId != id && sameName(r.name, clean.value())) return duplicate("consultation room");
        }
        repo_.updateRoom(id, clean.value(), dept);
        AuditEntryInput e;
        e.action = audit_action::kRoomUpdate;
        e.recordId = std::to_string(id);
        e.changedFields = {"name", "department"};
        audit_.append(session.userId, e);
        db_.faultPoint("before_commit");
        tx.commit();
        return *repo_.findRoom(id);
    });
}

Result<ConsultationRoom> AdminService::setRoomActive(const Session& session, std::int64_t id, bool active) {
    if (!authorize(session, Permission::ManageMasterData)) return deniedMaster(session);
    return guarded<ConsultationRoom>(true, [&]() -> Result<ConsultationRoom> {
        Transaction tx(db_);
        auto found = repo_.findRoom(id);
        if (!found) return roomMissing(id);
        if (found->active == active) return Error::conflict(active ? "That room is already in use." : "That room is already switched off.");
        repo_.setRoomActive(id, active);
        AuditEntryInput e;
        e.action = audit_action::kRoomUpdate;
        e.recordId = std::to_string(id);
        e.changedFields = {"active"};
        audit_.append(session.userId, e);
        db_.faultPoint("before_commit");
        tx.commit();
        found->active = active;
        return *found;
    });
}

Result<std::vector<DoctorMapping>> AdminService::listDoctorMappings(const Session& session) {
    if (!authorize(session, Permission::ManageMasterData)) return deniedMaster(session);
    return guarded<std::vector<DoctorMapping>>(false, [&]() -> Result<std::vector<DoctorMapping>> { return repo_.listDoctorMappings(); });
}

Result<DoctorMapping> AdminService::assignDoctorDepartment(const Session& session, std::int64_t doctorStaffId,
                                                           std::int64_t departmentId) {
    if (!authorize(session, Permission::ManageMasterData)) return deniedMaster(session);
    return guarded<DoctorMapping>(true, [&]() -> Result<DoctorMapping> {
        Transaction tx(db_);
        auto dept = repo_.findDepartment(departmentId);
        if (!dept) return departmentMissing(departmentId);
        if (!dept->active) return Error::conflict("That department is switched off.");
        if (!repo_.hasDoctorProfile(doctorStaffId)) {
            return Error::notFound("That doctor has no profile yet. Set the doctor's hours first (Admin option 2).");
        }
        repo_.setDoctorDepartment(doctorStaffId, dept->name, toIsoUtc(clock_.now()));
        AuditEntryInput e;
        e.action = audit_action::kDoctorDepartmentSet;
        e.recordId = std::to_string(doctorStaffId);
        e.changedFields = {"department"};
        audit_.append(session.userId, e);
        db_.faultPoint("before_commit");
        tx.commit();
        for (const auto& m : repo_.listDoctorMappings()) if (m.staffId == doctorStaffId) return m;
        return Error::notFound("That doctor's account is deactivated.");
    });
}

Result<HospitalReport> AdminService::report(const Session& session, const Date& from, const Date& to) {
    if (!authorize(session, Permission::ViewReports)) {
        return Error::accessDenied("Your role (" + to_string(session.role) + ") may not view reports.");
    }
    if (to < from) return Error::validation("To date", "must not be before the from date");
    const std::int64_t days = daysFromCivil(to.year, to.month, to.day) - daysFromCivil(from.year, from.month, from.day) + 1;
    if (days > policy_.report.maxRangeDays) {
        return Error::validation("Date range", "can cover at most " + std::to_string(policy_.report.maxRangeDays) + " days");
    }
    return guarded<HospitalReport>(false, [&]() -> Result<HospitalReport> {
        Transaction tx(db_);  // one consistent view of the store for the whole report
        auto r = repo_.report(from.toIso(), to.toIso());
        tx.commit();
        return r;
    });
}

}  // namespace hms
