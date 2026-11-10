// TC-Admin-02 (HMS-F-052): the Admin changes the appointment cutoff, the login lockout cooldown and
// the low-stock threshold, and the change applies to the next operation without a restart.
#include <doctest.h>

#include <chrono>
#include <sstream>

#include "admin/admin_service.hpp"
#include "auth/authentication.hpp"
#include "lab_pharmacy_test_support.hpp"
#include "ui/admin_screens.hpp"

using namespace hms;
using test::admin;
using test::at;
using test::kMonday;
using test::kTuesday;
using test::receptionist;

namespace {
struct SettingsFixture : test::LabPharmacyFixture {
    AdminService adminSvc{db(), clock, policy};

    std::string audit(const std::string& action) {
        auto st = db().prepare("SELECT changed_fields FROM audit_entry WHERE action = '" + action +
                               "' AND outcome = 'OK' ORDER BY recorded_at DESC, rowid DESC LIMIT 1");
        return st.step() ? st.getText(0) : std::string();
    }
};

std::string runSettings(SettingsFixture& f, const std::string& script, const Session& s = admin()) {
    std::istringstream in(script);
    std::ostringstream out;
    ConsoleIO io(in, out);
    settingsScreen(io, f.adminSvc, s);
    return out.str();
}
bool contains(const std::string& hay, const std::string& needle) { return hay.find(needle) != std::string::npos; }
}  // namespace

TEST_CASE("TC-Admin-02 the Admin sees the three settings with their built-in defaults") {
    SettingsFixture f;
    auto list = f.adminSvc.listSettings(admin());
    REQUIRE(list.ok());
    REQUIRE(list.value().size() == 3);
    CHECK(list.value()[0].info.id == SettingId::AppointmentCutoffMinutes);
    CHECK(list.value()[0].value == 60);
    CHECK(list.value()[1].value == 15);
    CHECK(list.value()[2].value == 10);
    for (const auto& s : list.value()) CHECK_FALSE(s.changed());
}

TEST_CASE("TC-Admin-02 a new value is checked, saved, audited with old and new value, and read back") {
    SettingsFixture f;
    auto saved = f.adminSvc.updateSetting(admin(), SettingId::AppointmentCutoffMinutes, " 120 ");
    REQUIRE(saved.ok());
    CHECK(saved.value().value == 120);
    CHECK(saved.value().changed());
    CHECK(f.adminSvc.listSettings(admin()).value()[0].value == 120);
    CHECK(f.audit("SETTINGS_UPDATE") == "appointment_cutoff_minutes 60 -> 120");
    CHECK(f.count("SELECT count(*) FROM system_parameter") == 1);

    // Changing it again updates the one row.
    REQUIRE(f.adminSvc.updateSetting(admin(), SettingId::AppointmentCutoffMinutes, "30").ok());
    CHECK(f.count("SELECT count(*) FROM system_parameter") == 1);
    CHECK(f.audit("SETTINGS_UPDATE") == "appointment_cutoff_minutes 120 -> 30");

    // The same value, text, a negative number and an out-of-range number are refused; nothing changes.
    CHECK(f.adminSvc.updateSetting(admin(), SettingId::AppointmentCutoffMinutes, "30").error().kind == ErrorKind::Conflict);
    for (const char* bad : {"", "abc", "-5", "1.5", "1441", "99999999999999999999"}) {
        auto r = f.adminSvc.updateSetting(admin(), SettingId::AppointmentCutoffMinutes, bad);
        REQUIRE_FALSE(r.ok());
        CHECK(r.error().kind == ErrorKind::Validation);
    }
    CHECK(f.adminSvc.updateSetting(admin(), SettingId::LockoutMinutes, "0").error().kind == ErrorKind::Validation);
    CHECK(f.adminSvc.listSettings(admin()).value()[0].value == 30);
    CHECK(f.count("SELECT count(*) FROM audit_entry WHERE action = 'SETTINGS_UPDATE' AND outcome = 'OK'") == 2);
}

TEST_CASE("TC-Admin-02 only the Admin may see or change settings; a refusal is audited") {
    SettingsFixture f;
    for (const Session& s : {receptionist(), test::doctorSession("mehta"), test::pharmacistSession(), test::labTech()}) {
        CHECK(f.adminSvc.listSettings(s).error().kind == ErrorKind::AccessDenied);
        CHECK(f.adminSvc.updateSetting(s, SettingId::LowStockThreshold, "5").error().kind == ErrorKind::AccessDenied);
    }
    CHECK(f.count("SELECT count(*) FROM system_parameter") == 0);
    CHECK(f.count("SELECT count(*) FROM audit_entry WHERE action = 'SETTINGS_UPDATE' AND outcome = 'DENIED'") == 8);
}

TEST_CASE("TC-Admin-02 a store failure while saving leaves the old value") {
    SettingsFixture f;
    f.db().setFaultHook([](const char* point) {
        if (std::string(point) == "before_commit") throw InjectedFault(point);
    });
    auto r = f.adminSvc.updateSetting(admin(), SettingId::LowStockThreshold, "25");
    f.db().setFaultHook(nullptr);
    REQUIRE_FALSE(r.ok());
    CHECK(f.adminSvc.listSettings(admin()).value()[2].value == 10);
    CHECK(f.count("SELECT count(*) FROM system_parameter") == 0);
}

TEST_CASE("TC-Admin-02 the appointment cutoff applies to the next cancel without a restart") {
    SettingsFixture f;
    const auto doc = f.addDoctor("mehta");
    const auto ravi = f.addPatient("Ravi Kumar");
    const auto appt = f.bookOk(ravi, doc, at(kTuesday, 9, 0));
    f.setLocalTime(kMonday, 10, 0);  // 23 hours before the start

    // Changes close 24 hours before: this appointment can no longer be changed.
    REQUIRE(f.adminSvc.updateSetting(admin(), SettingId::AppointmentCutoffMinutes, "1440").ok());
    auto refused = f.appointments->cancel(receptionist(), appt.id);
    REQUIRE_FALSE(refused.ok());
    CHECK(refused.error().kind == ErrorKind::Conflict);
    CHECK(refused.error().message.find("1440 minutes") != std::string::npos);
    CHECK(f.appointments->cutoffMinutes() == 1440);

    // The Admin shortens it: the same appointment can be cancelled now, in the same running service.
    REQUIRE(f.adminSvc.updateSetting(admin(), SettingId::AppointmentCutoffMinutes, "60").ok());
    CHECK(f.appointments->cancel(receptionist(), appt.id).ok());
}

TEST_CASE("TC-Admin-02 the lockout cooldown applies to the next lockout") {
    SettingsFixture f;
    REQUIRE(auth::createStaffAccount(f.db(), "doctor1", "SecurePass123!", auth::Role::Doctor, f.clock));
    REQUIRE(f.adminSvc.updateSetting(admin(), SettingId::LockoutMinutes, "2").ok());

    for (int i = 0; i < 5; ++i) auth::login(f.db(), "doctor1", "wrong-password", f.clock, f.policy.auth);
    CHECK(auth::login(f.db(), "doctor1", "SecurePass123!", f.clock, f.policy.auth).status == auth::LoginStatus::AccountLocked);
    f.clock.advance(std::chrono::minutes(1));
    CHECK(auth::login(f.db(), "doctor1", "SecurePass123!", f.clock, f.policy.auth).status == auth::LoginStatus::AccountLocked);
    f.clock.advance(std::chrono::minutes(2));  // 3 minutes in: well past 2, well short of the default 15
    CHECK(auth::login(f.db(), "doctor1", "SecurePass123!", f.clock, f.policy.auth).status == auth::LoginStatus::Success);
}

TEST_CASE("TC-Admin-02 the low-stock threshold applies to the next stock check") {
    SettingsFixture f;
    const auto med = f.addMedicine("Paracetamol 500 mg", "tablet", "8");
    CHECK(med.lowStock);  // 8 is below the default 10
    CHECK(f.pharmacy.lowStockThreshold() == 10);

    REQUIRE(f.adminSvc.updateSetting(admin(), SettingId::LowStockThreshold, "5").ok());
    CHECK(f.pharmacy.lowStockThreshold() == 5);
    auto stock = f.pharmacy.listStock(test::pharmacistSession());
    REQUIRE(stock.ok());
    CHECK_FALSE(stock.value()[0].lowStock);  // 8 is not below 5

    REQUIRE(f.adminSvc.updateSetting(admin(), SettingId::LowStockThreshold, "50").ok());
    CHECK(f.pharmacy.listStock(test::pharmacistSession()).value()[0].lowStock);
    CHECK(f.pharmacy.restock(test::pharmacistSession(), med.medicineId, "1").value().lowStock);
}

TEST_CASE("Console TC-Admin-02: the Admin changes a setting through the screen") {
    SettingsFixture f;
    const std::string out = runSettings(f,
                                        "9\n"      // not a setting -> asked again
                                        "3\n"      // low-stock threshold
                                        "abc\n"    // refused
                                        "3\n"
                                        "25\n"     // saved
                                        "1\n"
                                        "\n"       // Enter: cancelled
                                        "0\n");
    CHECK(contains(out, "SYSTEM SETTINGS"));
    CHECK(contains(out, "Now: 60 minutes"));
    CHECK(contains(out, "Please choose a number from 0 to 3."));
    CHECK(contains(out, "Value: "));
    CHECK(contains(out, "Saved: low_stock_threshold is now 25 units."));
    CHECK(contains(out, "Now: 25 units"));
    CHECK(contains(out, "Cancelled. Nothing was changed."));
    CHECK(f.pharmacy.lowStockThreshold() == 25);
    CHECK(f.count("SELECT count(*) FROM system_parameter") == 1);

    const std::string denied = runSettings(f, "0\n", receptionist());
    CHECK(contains(denied, "may not change system settings"));
    CHECK_FALSE(contains(denied, "SYSTEM SETTINGS"));
}
