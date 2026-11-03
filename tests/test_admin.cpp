// TC-Rep-01 (summary reports), TC-Staff-01 (staff accounts) and TC-Sec-02 (role restriction) for
// issue #9 (SRS HMS-F-023, HMS-F-051, HMS-SR-001, HMS-SR-004). Integration tests against a real
// SQLite store, and scripted sessions of the Admin screens.
#include <doctest.h>

#include <sstream>
#include <string>

#include "admin/admin_service.hpp"
#include "auth/authentication.hpp"
#include "billing_test_support.hpp"
#include "lab_pharmacy_test_support.hpp"
#include "ui/admin_screens.hpp"

using namespace hms;
using test::admin;
using test::at;
using test::kMonday;
using test::kToday;
using test::kTuesday;
using test::receptionist;

namespace {
struct AdminFixture : test::BillingFixture {
    AdminService adminSvc{db(), clock, policy};

    std::int64_t staffIdOf(const std::string& username) {
        auto st = db().prepare("SELECT staff_id FROM staff_accounts WHERE username = ?");
        st.bind(1, username);
        REQUIRE(st.step());
        return st.getInt64(0);
    }
    std::int64_t newAccount(const std::string& username, auth::Role role = auth::Role::Receptionist) {
        REQUIRE(auth::createStaffAccount(db(), username, "SecurePass123!", role, clock, "setup"));
        return staffIdOf(username);
    }
    auth::LoginStatus tryLogin(const std::string& username, const std::string& password) {
        return auth::login(db(), username, password, clock).status;
    }
    std::int64_t auditCount(const std::string& action, const std::string& outcome) {
        return count("SELECT count(*) FROM audit_entry WHERE action = '" + action + "' AND outcome = '" + outcome + "'");
    }

    // Monday 26-10-2026: one completed and billed visit (Rs 500, Rs 200 cash + Rs 100 card paid), one
    // booked appointment, and on Tuesday one cancelled appointment.
    void seedHospital() {
        const auto doc = addDoctor("mehta");
        setFee(doc, "500");
        const auto ravi = addPatient("Ravi Kumar");
        const auto asha = addPatient("Asha Rao");
        const auto meena = addPatient("Meena Iyer");
        const auto done = completedVisit(ravi, doc, "mehta");  // clock is now Monday 09:30
        const auto bill = billOk(done.id);
        REQUIRE(billing.recordPayment(receptionist(), bill.billId, "200", PaymentMode::Cash).ok());
        REQUIRE(billing.recordPayment(receptionist(), bill.billId, "100", PaymentMode::Card).ok());
        bookOk(asha, doc, at(kMonday, 10, 0));
        const auto cancelled = bookOk(meena, doc, at(kTuesday, 9, 0));
        REQUIRE(appointments->cancel(receptionist(), cancelled.id).ok());
    }
};

std::string runStaffScreen(AdminFixture& f, const std::string& script, const Session& s,
                           const std::string& passwords = "") {
    std::istringstream in(script);
    std::istringstream pw(passwords);
    std::ostringstream out;
    ConsoleIO io(in, out);
    PasswordReader reader = [&pw](const std::string&, std::string& p) { return static_cast<bool>(std::getline(pw, p)); };
    staffAccountsScreen(io, f.adminSvc, s, reader);
    return out.str();
}
std::string runReportScreen(AdminFixture& f, const std::string& script, const Session& s) {
    std::istringstream in(script);
    std::ostringstream out;
    ConsoleIO io(in, out);
    reportsScreen(io, f.adminSvc, s);
    return out.str();
}
std::string runMasterScreen(AdminFixture& f, const std::string& script, const Session& s) {
    std::istringstream in(script);
    std::ostringstream out;
    ConsoleIO io(in, out);
    masterDataScreen(io, f.adminSvc, s);
    return out.str();
}
bool contains(const std::string& hay, const std::string& needle) { return hay.find(needle) != std::string::npos; }
}  // namespace

TEST_CASE("TC-Rep-01 the report totals match the stored appointments, bills and payments") {
    AdminFixture f;
    f.seedHospital();

    auto r = f.adminSvc.report(admin(), kMonday, kTuesday);
    REQUIRE(r.ok());
    const HospitalReport& rep = r.value();
    CHECK(rep.registeredPatients == f.count("SELECT count(*) FROM patient"));
    CHECK(rep.appointments == 3);
    CHECK(rep.completed == f.count("SELECT count(*) FROM appointment WHERE status = 'COMPLETED'"));
    CHECK(rep.booked == 1);
    CHECK(rep.cancelled == 1);
    CHECK(rep.noShow == 0);
    CHECK(rep.patientsSeen == 2);  // the cancelled appointment's patient was not seen
    CHECK(rep.consultations == f.count("SELECT count(*) FROM consultation"));
    CHECK(rep.billsCount == 1);
    CHECK(rep.billedPaise == f.count("SELECT sum(total_paise) FROM bill"));
    CHECK(rep.billedPaise == 50000);
    CHECK(rep.paymentsCount == 2);
    CHECK(rep.collectedPaise == f.count("SELECT sum(amount_paise) FROM payment"));
    CHECK(rep.collectedPaise == 30000);
    CHECK(rep.outstandingPaise == 20000);
    REQUIRE(rep.byMode.size() == 2);
    CHECK(rep.byMode[0].mode == "CARD");
    CHECK(rep.byMode[0].paise == 10000);
    CHECK(rep.byMode[1].mode == "CASH");
    CHECK(rep.byMode[1].paise == 20000);
}

TEST_CASE("TC-Rep-01 daily and doctor-wise counts follow the selected dates") {
    AdminFixture f;
    f.seedHospital();

    auto both = f.adminSvc.report(admin(), kMonday, kTuesday);
    REQUIRE(both.ok());
    REQUIRE(both.value().days.size() == 2);
    CHECK(both.value().days[0].date == "2026-10-26");
    CHECK(both.value().days[0].patientsSeen == 2);
    CHECK(both.value().days[0].completed == 1);
    CHECK(both.value().days[0].booked == 1);
    CHECK(both.value().days[1].date == "2026-10-27");
    CHECK(both.value().days[1].cancelled == 1);
    CHECK(both.value().days[1].patientsSeen == 0);
    REQUIRE(both.value().doctors.size() == 1);
    CHECK(both.value().doctors[0].doctor == "mehta");
    CHECK(both.value().doctors[0].department == "General Medicine");
    CHECK(both.value().doctors[0].total() == 3);

    auto tuesday = f.adminSvc.report(admin(), kTuesday, kTuesday);
    REQUIRE(tuesday.ok());
    CHECK(tuesday.value().appointments == 1);
    CHECK(tuesday.value().cancelled == 1);
    CHECK(tuesday.value().billsCount == 0);
    CHECK(tuesday.value().collectedPaise == 0);
    CHECK(tuesday.value().byMode.empty());

    auto before = f.adminSvc.report(admin(), kToday, kToday);  // Sunday: nothing happened
    REQUIRE(before.ok());
    CHECK(before.value().appointments == 0);
    CHECK(before.value().days.empty());
    CHECK(before.value().doctors.empty());
    CHECK(before.value().registeredPatients == 3);
}

TEST_CASE("TC-Rep-01 a payment is counted on the hospital-local date, not the UTC date") {
    AdminFixture f;
    f.seedHospital();
    // 00:30 IST on Tuesday is 19:00 UTC on Monday.
    f.setLocalTime(kTuesday, 0, 30);
    REQUIRE(f.billing.recordPayment(test::receptionist(), 1, "50", PaymentMode::Cash).ok());
    auto tuesday = f.adminSvc.report(admin(), kTuesday, kTuesday);
    REQUIRE(tuesday.ok());
    CHECK(tuesday.value().collectedPaise == 5000);
    auto monday = f.adminSvc.report(admin(), kMonday, kMonday);
    REQUIRE(monday.ok());
    CHECK(monday.value().collectedPaise == 30000);
}

TEST_CASE("TC-Rep-01 a bad date range is refused and reading changes nothing") {
    AdminFixture f;
    f.seedHospital();
    CHECK(f.adminSvc.report(admin(), kTuesday, kMonday).error().kind == ErrorKind::Validation);
    CHECK(f.adminSvc.report(admin(), Date{2026, 1, 1}, Date{2027, 1, 2}).error().kind == ErrorKind::Validation);
    CHECK(f.adminSvc.report(admin(), Date{2026, 1, 1}, Date{2026, 12, 31}).ok());  // 365 days is allowed

    const auto audits = f.count("SELECT count(*) FROM audit_entry");
    const auto payments = f.count("SELECT count(*) FROM payment");
    REQUIRE(f.adminSvc.report(admin(), kMonday, kTuesday).ok());
    CHECK(f.count("SELECT count(*) FROM audit_entry") == audits);
    CHECK(f.count("SELECT count(*) FROM payment") == payments);
}

TEST_CASE("TC-Sec-02 only the Admin may view reports or manage staff accounts, and every denial is audited") {
    AdminFixture f;
    f.seedHospital();
    const auto id = f.staffIdOf("mehta");
    int roles = 0;
    for (const Session& s : {test::receptionist(), test::doctorSession("mehta"), test::pharmacist(), test::labTech()}) {
        CHECK(f.adminSvc.report(s, kMonday, kTuesday).error().kind == ErrorKind::AccessDenied);
        CHECK(f.adminSvc.listStaff(s).error().kind == ErrorKind::AccessDenied);
        CHECK(f.adminSvc.deactivate(s, id).error().kind == ErrorKind::AccessDenied);
        CHECK(f.adminSvc.reactivate(s, id).error().kind == ErrorKind::AccessDenied);
        CHECK(f.adminSvc.unlock(s, id).error().kind == ErrorKind::AccessDenied);
        CHECK(f.adminSvc.resetPassword(s, id, "NewSecure123!").error().kind == ErrorKind::AccessDenied);
        ++roles;
    }
    CHECK(f.auditCount("REPORT_VIEW", "DENIED") == roles);
    CHECK(f.auditCount("STAFF_ACCOUNT_MANAGE", "DENIED") == roles * 5);
    CHECK(f.count("SELECT active FROM staff_accounts WHERE staff_id = " + std::to_string(id)) == 1);
}

TEST_CASE("TC-Staff-01 the Admin lists staff accounts with role and status") {
    AdminFixture f;
    f.newAccount("admin1", auth::Role::Administrator);
    f.newAccount("reena");
    f.newAccount("pooja", auth::Role::Pharmacist);
    auto r = f.adminSvc.listStaff(admin());
    REQUIRE(r.ok());
    REQUIRE(r.value().size() == 3);
    CHECK(r.value()[0].username == "admin1");
    CHECK(r.value()[0].role == "Administrator");
    CHECK(r.value()[1].username == "pooja");
    CHECK(r.value()[1].role == "Pharmacist");
    CHECK(r.value()[2].role == "Receptionist");
    CHECK(r.value()[2].active);
    CHECK_FALSE(r.value()[2].locked);
}

TEST_CASE("TC-Staff-01 a deactivated account cannot log in and the action is audited") {
    AdminFixture f;
    f.newAccount("admin1", auth::Role::Administrator);
    const auto id = f.newAccount("reena");
    REQUIRE(f.tryLogin("reena", "SecurePass123!") == auth::LoginStatus::Success);

    auto r = f.adminSvc.deactivate(admin(), id);
    REQUIRE(r.ok());
    CHECK_FALSE(r.value().active);
    CHECK(f.tryLogin("reena", "SecurePass123!") != auth::LoginStatus::Success);
    CHECK(f.count("SELECT count(*) FROM audit_entry WHERE action = 'STAFF_LOGIN' AND reason_code = 'ACCOUNT_DEACTIVATED'") == 1);
    CHECK(f.auditCount("STAFF_ACCOUNT_DEACTIVATE", "OK") == 1);
    CHECK(f.count("SELECT failed_attempts FROM staff_accounts WHERE staff_id = " + std::to_string(id)) == 0);

    CHECK(f.adminSvc.deactivate(admin(), id).error().kind == ErrorKind::Conflict);  // already deactivated
    auto back = f.adminSvc.reactivate(admin(), id);
    REQUIRE(back.ok());
    CHECK(back.value().active);
    CHECK(f.tryLogin("reena", "SecurePass123!") == auth::LoginStatus::Success);
    CHECK(f.auditCount("STAFF_ACCOUNT_REACTIVATE", "OK") == 1);
    CHECK(f.adminSvc.reactivate(admin(), id).error().kind == ErrorKind::Conflict);
}

TEST_CASE("TC-Staff-01 the Admin cannot lock themselves out") {
    AdminFixture f;
    const auto first = f.newAccount("admin1", auth::Role::Administrator);
    const auto second = f.newAccount("admin2", auth::Role::Administrator);
    const Session a1{"admin1", "admin1", Role::Admin, false};

    CHECK(f.adminSvc.deactivate(a1, first).error().kind == ErrorKind::Conflict);  // their own account
    REQUIRE(f.adminSvc.deactivate(a1, second).ok());
    // admin2 is now deactivated, so admin1 is the last active Administrator.
    CHECK(f.adminSvc.deactivate(Session{"other", "other", Role::Admin, false}, first).error().kind == ErrorKind::Conflict);
    CHECK(f.count("SELECT count(*) FROM staff_accounts WHERE role = 'administrator' AND active = 1") == 1);
    CHECK(f.adminSvc.deactivate(a1, 9999).error().kind == ErrorKind::NotFound);
}

TEST_CASE("TC-Staff-01 unlock and password reset") {
    AdminFixture f;
    f.newAccount("admin1", auth::Role::Administrator);
    const auto id = f.newAccount("reena");
    for (int i = 0; i < 5; ++i) f.tryLogin("reena", "wrong");
    CHECK(f.tryLogin("reena", "SecurePass123!") == auth::LoginStatus::AccountLocked);
    auto listed = f.adminSvc.listStaff(admin());
    REQUIRE(listed.ok());
    bool sawLocked = false;
    for (const auto& m : listed.value()) sawLocked = sawLocked || (m.username == "reena" && m.locked);
    CHECK(sawLocked);

    REQUIRE(f.adminSvc.unlock(admin(), id).ok());
    CHECK(f.tryLogin("reena", "SecurePass123!") == auth::LoginStatus::Success);
    CHECK(f.auditCount("STAFF_ACCOUNT_UNLOCK", "OK") == 1);

    CHECK(f.adminSvc.resetPassword(admin(), id, "short").error().kind == ErrorKind::Validation);
    REQUIRE(f.adminSvc.resetPassword(admin(), id, "BrandNew123!").ok());
    CHECK(f.tryLogin("reena", "SecurePass123!") != auth::LoginStatus::Success);
    CHECK(f.tryLogin("reena", "BrandNew123!") == auth::LoginStatus::Success);
    CHECK(f.auditCount("STAFF_PASSWORD_RESET", "OK") == 1);
    CHECK(f.count("SELECT count(*) FROM audit_entry WHERE changed_fields LIKE '%BrandNew%'") == 0);
    CHECK(f.adminSvc.resetPassword(admin(), 9999, "BrandNew123!").error().kind == ErrorKind::NotFound);
}

TEST_CASE("TC-Staff-01 a failed save leaves the account unchanged") {
    AdminFixture f;
    f.newAccount("admin1", auth::Role::Administrator);
    const auto id = f.newAccount("reena");
    f.db().setFaultHook([](const char* p) { if (std::string(p) == "before_commit") throw InjectedFault(p); });
    CHECK(f.adminSvc.deactivate(admin(), id).error().kind == ErrorKind::StorageFailure);
    f.db().setFaultHook(nullptr);
    CHECK(f.count("SELECT active FROM staff_accounts WHERE staff_id = " + std::to_string(id)) == 1);
    CHECK(f.auditCount("STAFF_ACCOUNT_DEACTIVATE", "OK") == 0);
}

TEST_CASE("TC-Staff-01 the staff screen deactivates an account after confirmation") {
    AdminFixture f;
    f.newAccount("admin1", auth::Role::Administrator);
    f.newAccount("reena");
    // Row 2 is reena (names are listed in order), option 1, confirm, then Enter to leave.
    const auto out = runStaffScreen(f, "2\n1\ny\n\n", admin());
    CHECK(contains(out, "reena is deactivated"));
    CHECK(contains(out, "deactivated"));
    CHECK(f.tryLogin("reena", "SecurePass123!") != auth::LoginStatus::Success);

    const auto declined = runStaffScreen(f, "2\n1\nn\n\n", admin());
    CHECK(contains(declined, "Nothing changed"));
    const auto pw = runStaffScreen(f, "2\n3\n\n", admin(), "NewSecure123!\nNewSecure123!\n");
    CHECK(contains(pw, "password for reena is changed"));
    const auto mismatch = runStaffScreen(f, "2\n3\n\n", admin(), "NewSecure123!\nOther123456!\n");
    CHECK(contains(mismatch, "Passwords do not match"));
    const auto denied = runStaffScreen(f, "", test::receptionist());
    CHECK(contains(denied, "may not manage staff accounts"));
}

TEST_CASE("TC-Rep-01 the report screen prints the summary") {
    AdminFixture f;
    f.seedHospital();
    const auto out = runReportScreen(f, "26-10-2026\n27-10-2026\n", admin());
    CHECK(contains(out, "HOSPITAL SUMMARY, 26-10-2026 to 27-10-2026"));
    CHECK(contains(out, "Registered patients (all time):   3"));
    CHECK(contains(out, "Total 3   Booked 1   Completed 1   Cancelled 1   No-show 0"));
    CHECK(contains(out, "Rs 300.00"));
    CHECK(contains(out, "Still unpaid on those bills: Rs 200.00"));
    CHECK(contains(out, "Dr mehta"));

    CHECK(contains(runReportScreen(f, "27-10-2026\n26-10-2026\n", admin()), "must not be before"));
    CHECK(contains(runReportScreen(f, "26-10-2026\n27-10-2026\n", test::receptionist()), "may not view reports"));
}

TEST_CASE("TC-Admin-01 the Admin adds departments; names are checked and unique ignoring case") {
    AdminFixture f;
    auto a = f.adminSvc.addDepartment(admin(), "  Cardiology ");
    REQUIRE(a.ok());
    CHECK(a.value().name == "Cardiology");
    CHECK(f.adminSvc.addDepartment(admin(), "cardiology").error().kind == ErrorKind::Conflict);
    CHECK(f.adminSvc.addDepartment(admin(), "").error().kind == ErrorKind::Validation);
    CHECK(f.adminSvc.addDepartment(admin(), std::string(61, 'x')).error().kind == ErrorKind::Validation);
    CHECK(f.adminSvc.addDepartment(admin(), std::string(60, 'x')).ok());
    auto list = f.adminSvc.listDepartments(admin());
    REQUIRE(list.ok());
    CHECK(list.value().size() == 2);
    CHECK(f.auditCount("DEPARTMENT_CREATE", "OK") == 2);
}

TEST_CASE("TC-Admin-01 renaming a department shows in booking and billing at once") {
    AdminFixture f;
    const auto doc = f.addDoctor("mehta", "General Medicine");
    f.setFee(doc, "500");
    auto dept = f.adminSvc.addDepartment(admin(), "General Medicine");
    REQUIRE(dept.ok());
    CHECK(f.adminSvc.listDepartments(admin()).value()[0].doctors == 1);

    REQUIRE(f.adminSvc.renameDepartment(admin(), dept.value().departmentId, "Internal Medicine").ok());
    auto doctors = f.appointments->listDoctors(admin());
    REQUIRE(doctors.ok());
    CHECK(doctors.value()[0].department == "Internal Medicine");
    auto fees = f.billing.listDoctorFees(admin());
    REQUIRE(fees.ok());
    CHECK(fees.value()[0].department == "Internal Medicine");
    const auto ravi = f.addPatient("Ravi Kumar");
    CHECK(f.bookOk(ravi, doc, at(kMonday, 9, 0)).department == "Internal Medicine");
    CHECK(f.auditCount("DEPARTMENT_UPDATE", "OK") == 1);

    auto other = f.adminSvc.addDepartment(admin(), "Cardiology");
    REQUIRE(other.ok());
    CHECK(f.adminSvc.renameDepartment(admin(), other.value().departmentId, "internal medicine").error().kind == ErrorKind::Conflict);
    CHECK(f.adminSvc.renameDepartment(admin(), 999, "X").error().kind == ErrorKind::NotFound);
}

TEST_CASE("TC-Admin-01 the Admin moves a doctor to another department") {
    AdminFixture f;
    const auto doc = f.addDoctor("mehta", "General Medicine");
    const auto noProfile = f.addStaff("rao");
    auto cardio = f.adminSvc.addDepartment(admin(), "Cardiology");
    REQUIRE(cardio.ok());

    auto moved = f.adminSvc.assignDoctorDepartment(admin(), doc, cardio.value().departmentId);
    REQUIRE(moved.ok());
    CHECK(moved.value().department == "Cardiology");
    CHECK(f.appointments->listDoctors(admin()).value()[0].department == "Cardiology");
    CHECK(f.auditCount("DOCTOR_DEPARTMENT_SET", "OK") == 1);
    CHECK(f.adminSvc.assignDoctorDepartment(admin(), noProfile, cardio.value().departmentId).error().kind == ErrorKind::NotFound);
    CHECK(f.adminSvc.assignDoctorDepartment(admin(), doc, 999).error().kind == ErrorKind::NotFound);

    // A department with a doctor in it cannot be switched off; an empty one can, and then takes no doctor.
    CHECK(f.adminSvc.setDepartmentActive(admin(), cardio.value().departmentId, false).error().kind == ErrorKind::Conflict);
    auto empty = f.adminSvc.addDepartment(admin(), "Dermatology");
    REQUIRE(empty.ok());
    REQUIRE(f.adminSvc.setDepartmentActive(admin(), empty.value().departmentId, false).ok());
    CHECK(f.adminSvc.assignDoctorDepartment(admin(), doc, empty.value().departmentId).error().kind == ErrorKind::Conflict);
    CHECK(f.adminSvc.setDepartmentActive(admin(), empty.value().departmentId, false).error().kind == ErrorKind::Conflict);
    REQUIRE(f.adminSvc.setDepartmentActive(admin(), empty.value().departmentId, true).ok());
}

TEST_CASE("TC-Admin-01 consultation rooms: add, edit, switch off") {
    AdminFixture f;
    auto cardio = f.adminSvc.addDepartment(admin(), "Cardiology");
    REQUIRE(cardio.ok());
    auto room = f.adminSvc.addRoom(admin(), "Room 1", cardio.value().departmentId);
    REQUIRE(room.ok());
    CHECK(room.value().department == "Cardiology");
    auto free = f.adminSvc.addRoom(admin(), "Room 2", 0);
    REQUIRE(free.ok());
    CHECK(free.value().department.empty());
    CHECK(f.adminSvc.addRoom(admin(), "room 1", 0).error().kind == ErrorKind::Conflict);
    CHECK(f.adminSvc.addRoom(admin(), "", 0).error().kind == ErrorKind::Validation);
    CHECK(f.adminSvc.addRoom(admin(), "Room 3", 999).error().kind == ErrorKind::NotFound);

    auto edited = f.adminSvc.updateRoom(admin(), free.value().roomId, "Room 2A", cardio.value().departmentId);
    REQUIRE(edited.ok());
    CHECK(edited.value().name == "Room 2A");
    CHECK(edited.value().department == "Cardiology");
    CHECK(f.adminSvc.updateRoom(admin(), free.value().roomId, "ROOM 1", 0).error().kind == ErrorKind::Conflict);
    CHECK(f.adminSvc.updateRoom(admin(), 999, "X", 0).error().kind == ErrorKind::NotFound);

    REQUIRE(f.adminSvc.setRoomActive(admin(), room.value().roomId, false).ok());
    CHECK(f.adminSvc.setRoomActive(admin(), room.value().roomId, false).error().kind == ErrorKind::Conflict);
    auto rooms = f.adminSvc.listRooms(admin());
    REQUIRE(rooms.ok());
    REQUIRE(rooms.value().size() == 2);
    CHECK_FALSE(rooms.value()[0].active);  // "Room 1" sorts first
    CHECK(f.auditCount("ROOM_CREATE", "OK") == 2);
    CHECK(f.auditCount("ROOM_UPDATE", "OK") == 2);
}

TEST_CASE("TC-Admin-01 only the Admin may change master data, and every denial is audited") {
    AdminFixture f;
    int roles = 0;
    for (const Session& s : {test::receptionist(), test::doctorSession("mehta"), test::pharmacist(), test::labTech()}) {
        CHECK(f.adminSvc.listDepartments(s).error().kind == ErrorKind::AccessDenied);
        CHECK(f.adminSvc.addDepartment(s, "X").error().kind == ErrorKind::AccessDenied);
        CHECK(f.adminSvc.renameDepartment(s, 1, "X").error().kind == ErrorKind::AccessDenied);
        CHECK(f.adminSvc.setDepartmentActive(s, 1, false).error().kind == ErrorKind::AccessDenied);
        CHECK(f.adminSvc.listRooms(s).error().kind == ErrorKind::AccessDenied);
        CHECK(f.adminSvc.addRoom(s, "X", 0).error().kind == ErrorKind::AccessDenied);
        CHECK(f.adminSvc.updateRoom(s, 1, "X", 0).error().kind == ErrorKind::AccessDenied);
        CHECK(f.adminSvc.setRoomActive(s, 1, false).error().kind == ErrorKind::AccessDenied);
        CHECK(f.adminSvc.listDoctorMappings(s).error().kind == ErrorKind::AccessDenied);
        CHECK(f.adminSvc.assignDoctorDepartment(s, 1, 1).error().kind == ErrorKind::AccessDenied);
        ++roles;
    }
    CHECK(f.auditCount("MASTER_DATA_MANAGE", "DENIED") == roles * 10);
    CHECK(f.count("SELECT count(*) FROM department") == 0);
}

TEST_CASE("TC-Admin-01 a failed save leaves the master data unchanged") {
    AdminFixture f;
    f.db().setFaultHook([](const char* p) { if (std::string(p) == "before_commit") throw InjectedFault(p); });
    CHECK(f.adminSvc.addDepartment(admin(), "Cardiology").error().kind == ErrorKind::StorageFailure);
    CHECK(f.adminSvc.addRoom(admin(), "Room 1", 0).error().kind == ErrorKind::StorageFailure);
    f.db().setFaultHook(nullptr);
    CHECK(f.count("SELECT count(*) FROM department") == 0);
    CHECK(f.count("SELECT count(*) FROM consultation_room") == 0);
}

TEST_CASE("TC-Admin-01 the master data screens") {
    AdminFixture f;
    f.addDoctor("mehta", "General Medicine");
    const auto added = runMasterScreen(f, "1\na\nCardiology\n\n0\n", admin());
    CHECK(contains(added, "Department added"));
    CHECK(contains(added, "Cardiology"));
    const auto renamed = runMasterScreen(f, "1\nr\n1\nHeart Care\n\n0\n", admin());
    CHECK(contains(renamed, "Booking and billing show the new name now"));
    const auto room = runMasterScreen(f, "2\na\nRoom 1\n1\n\n0\n", admin());
    CHECK(contains(room, "Room saved"));
    CHECK(contains(room, "Heart Care"));
    const auto mapped = runMasterScreen(f, "3\n1\n1\n\n0\n", admin());
    CHECK(contains(mapped, "Dr mehta is now in Heart Care"));
    CHECK(f.appointments->listDoctors(admin()).value()[0].department == "Heart Care");
    const auto denied = runMasterScreen(f, "1\n\n0\n", test::receptionist());
    CHECK(contains(denied, "may not manage master data"));
}
