// TC-Sec-05 (Admin's read-only view of the audit log) for issue #6
// (SRS HMS-SR-004, HMS-SR-001). Integration tests against a real SQLite store,
// and a scripted session of the Admin screen.
#include <doctest.h>

#include <sstream>
#include <string>

#include "appointment_test_support.hpp"
#include "audit/audit_log_service.hpp"
#include "ui/audit_log_screen.hpp"

using namespace hms;
using test::AppointmentFixture;

namespace {
struct AuditViewFixture : AppointmentFixture {
    AuditLogService auditLog{db(), clock, policy};
};

std::string runScreen(AuditViewFixture& f, const std::string& script, const Session& s) {
    std::istringstream in(script);
    std::ostringstream out;
    ConsoleIO io(in, out);
    auditLogScreen(io, f.auditLog, s);
    return out.str();
}
bool contains(const std::string& hay, const std::string& needle) { return hay.find(needle) != std::string::npos; }
}  // namespace

TEST_CASE("TC-Sec-05 the Admin reads the audit log, newest first") {
    AuditViewFixture f;
    f.addPatient("Ravi Kumar");
    f.addPatient("Asha Rao");

    auto r = f.auditLog.recentEntries(test::admin(), 50);
    REQUIRE(r.ok());
    CHECK(static_cast<std::int64_t>(r.value().size()) == f.count("SELECT count(*) FROM audit_entry"));
    REQUIRE(r.value().size() >= 2);
    CHECK(r.value()[0].action == "PATIENT_REGISTER");
    CHECK(r.value()[0].recordId.value_or("") == "HMS-000002");  // the newest entry comes first
    CHECK(r.value()[1].recordId.value_or("") == "HMS-000001");

    auto one = f.auditLog.recentEntries(test::admin(), 1);
    REQUIRE(one.ok());
    CHECK(one.value().size() == 1);
}

TEST_CASE("TC-Sec-05 reading the log changes nothing") {
    AuditViewFixture f;
    f.addPatient("Ravi Kumar");
    const auto before = f.count("SELECT count(*) FROM audit_entry");
    REQUIRE(f.auditLog.recentEntries(test::admin(), 50).ok());
    REQUIRE(f.auditLog.recentEntries(test::admin(), 50).ok());
    CHECK(f.count("SELECT count(*) FROM audit_entry") == before);
}

TEST_CASE("TC-Sec-05 every role other than Admin is denied, and each denial is audited") {
    AuditViewFixture f;
    for (const Session& s : {test::receptionist(), test::doctorSession("mehta"), test::pharmacist()}) {
        auto r = f.auditLog.recentEntries(s, 50);
        REQUIRE_FALSE(r.ok());
        CHECK(r.error().kind == ErrorKind::AccessDenied);
        CHECK(f.count("SELECT count(*) FROM audit_entry WHERE action = 'AUDIT_LOG_VIEW' AND outcome = 'DENIED' "
                      "AND user_id = '" + s.userId + "'") == 1);
    }
}

TEST_CASE("TC-Sec-05 the number of entries must be from 1 to the maximum") {
    AuditViewFixture f;
    const std::size_t maxEntries = f.policy.auditView.maxEntries;
    CHECK(f.auditLog.recentEntries(test::admin(), 0).error().kind == ErrorKind::Validation);
    CHECK(f.auditLog.recentEntries(test::admin(), maxEntries + 1).error().kind == ErrorKind::Validation);
    CHECK(f.auditLog.recentEntries(test::admin(), maxEntries).ok());
}

TEST_CASE("Console TC-Sec-05: the Admin screen lists the entries with time, user, action, record and outcome") {
    AuditViewFixture f;
    f.addPatient("Ravi Kumar");
    // A denied attempt, so that a reason-free DENIED row is listed too.
    REQUIRE_FALSE(f.auditLog.recentEntries(test::receptionist(), 5).ok());

    const std::string out = runScreen(f, "abc\n\n", test::admin());  // not a number -> asked again; Enter = default
    CHECK(contains(out, "Number of entries"));
    CHECK(contains(out, "AUDIT LOG (newest first, read only)"));
    CHECK(contains(out, "AUDIT_LOG_VIEW"));
    CHECK(contains(out, "DENIED"));
    CHECK(contains(out, "PATIENT_REGISTER"));
    CHECK(contains(out, "HMS-000001"));
    CHECK(contains(out, "25-10-2026 11:30"));  // stored in UTC, shown in hospital-local time
    CHECK_FALSE(contains(out, "Ravi"));       // entries never hold patient names

    const std::string two = runScreen(f, "2\n", test::admin());
    CHECK(contains(two, "2 entries shown."));
}

TEST_CASE("Console TC-Sec-05: another role using the screen is refused") {
    AuditViewFixture f;
    const std::string out = runScreen(f, "\n", test::receptionist());
    CHECK(contains(out, "may not view the audit log"));
    CHECK_FALSE(contains(out, "AUDIT LOG ("));
}
