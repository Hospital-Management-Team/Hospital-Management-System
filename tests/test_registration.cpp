// TC-Reg-02: UHID allocation and duplicate-candidate review
// (SRS v11 draft HMS-F-002, AC (1)-(9); SAD 4.2.2). Integration tests against a
// real SQLite store.
#include <doctest.h>

#include "patient/uhid.hpp"
#include "patient_test_support.hpp"

using namespace hms;
using hms::test::StoreFixture;

namespace {
using Status = RegistrationOutcome::Status;

std::int64_t registerOk(PatientService& svc, const PatientDraft& d) {
    auto r = svc.registerPatient(test::receptionist(), d, DuplicateDecision::none());
    REQUIRE(r.ok());
    REQUIRE(r.value().status == Status::Registered);
    return r.value().uhid;
}

DuplicateDecision differentPerson(const RegistrationOutcome& o, std::optional<DuplicateReason> reason) {
    DuplicateDecision d;
    d.kind = DuplicateDecision::Kind::DifferentPerson;
    d.reason = reason;
    for (const auto& c : o.candidates) d.reviewedUhids.push_back(c.patient.uhid);
    return d;
}
}  // namespace

TEST_CASE("TC-Reg-02 (1) no candidate: a new unique UHID is created and audited") {
    StoreFixture f;
    const auto a = registerOk(*f.service, test::draft("Ravi Kumar", "05-03-1990", "M", "9876543210"));
    const auto b = registerOk(*f.service, test::draft("Anita Rao", "11-11-1985", "F", "9123456789"));
    CHECK(a != b);
    CHECK(f.count("SELECT count(*) FROM patient") == 2);

    auto audit = AuditLog(f.db(), f.clock).recent(10);
    REQUIRE(audit.size() == 2);
    CHECK(audit[0].action == "PATIENT_REGISTER");
    CHECK(audit[0].recordId.value() == formatUhid(b));
    CHECK(audit[0].userId == "STAFF-R1");
    CHECK(audit[0].outcome == "OK");
    CHECK(audit[0].recordedAt == "2026-10-25T06:00:00Z");
    CHECK(audit[0].eventId.size() == 32);
}

TEST_CASE("TC-Reg-02 (2) strong candidate: nothing is saved until staff decide") {
    StoreFixture f;
    const auto first = registerOk(*f.service, test::draft("Ravi Kumar", "05-03-1990", "M", "9876543210"));

    // Same normalised name, DOB and gender; different phone (phone not needed for a strong match).
    auto d = test::draft("  ravi   KUMAR", "05-03-1990", "Male", "9000000001");
    auto r = f.service->registerPatient(test::receptionist(), d, DuplicateDecision::none());
    REQUIRE(r.ok());
    REQUIRE(r.value().status == Status::NeedsDecision);
    REQUIRE(r.value().candidates.size() == 1);
    CHECK(r.value().candidates[0].patient.uhid == first);
    CHECK(r.value().candidates[0].kind == CandidateKind::Strong);
    CHECK(r.value().reasonRequired);
    CHECK(f.count("SELECT count(*) FROM patient") == 1);  // (5) no record yet
    CHECK(f.count("SELECT count(*) FROM audit_entry") == 1);

    SUBCASE("Different person without a reason code is refused") {
        auto again = f.service->registerPatient(test::receptionist(), d, differentPerson(r.value(), std::nullopt));
        REQUIRE_FALSE(again.ok());
        CHECK(again.error().kind == ErrorKind::Validation);
        CHECK(f.count("SELECT count(*) FROM patient") == 1);
    }
    SUBCASE("Different person with a reason code creates a new UHID; decision stored with reviewed set") {
        auto again = f.service->registerPatient(test::receptionist(), d,
                                                differentPerson(r.value(), DuplicateReason::TwinsOrMultipleBirth));
        REQUIRE(again.ok());
        REQUIRE(again.value().status == Status::Registered);
        CHECK(again.value().uhid != first);
        CHECK(f.count("SELECT count(*) FROM patient") == 2);

        auto audit = AuditLog(f.db(), f.clock).recent(1).at(0);
        CHECK(audit.action == "PATIENT_REGISTER");
        CHECK(audit.reasonCode.value() == "TWINS_OR_MULTIPLE_BIRTH");
        CHECK(f.count("SELECT count(*) FROM duplicate_review WHERE decision = 'DIFFERENT_PERSON' AND created_uhid = " +
                      std::to_string(again.value().uhid) + " AND event_id = '" + audit.eventId + "'") == 1);
        CHECK(f.count("SELECT count(*) FROM duplicate_review_candidate WHERE uhid = " + std::to_string(first) +
                      " AND kind = 'STRONG'") == 1);
    }
    SUBCASE("Same patient: existing UHID is used, no record created, decision audited") {
        auto used = f.service->useExistingPatient(test::receptionist(), d, first, {first});
        REQUIRE(used.ok());
        CHECK(used.value().uhid == first);
        CHECK(f.count("SELECT count(*) FROM patient") == 1);
        auto audit = AuditLog(f.db(), f.clock).recent(1).at(0);
        CHECK(audit.action == "PATIENT_DUPLICATE_USE_EXISTING");
        CHECK(audit.recordId.value() == formatUhid(first));
        CHECK(f.count("SELECT count(*) FROM duplicate_review WHERE decision = 'USE_EXISTING' AND chosen_uhid = " +
                      std::to_string(first)) == 1);
    }
    SUBCASE("Same patient must be one of the candidates shown") {
        auto used = f.service->useExistingPatient(test::receptionist(), d, first, {});
        CHECK_FALSE(used.ok());
    }
}

TEST_CASE("TC-Reg-02 (3) phone-match only: shared numbers are allowed without a reason") {
    StoreFixture f;
    const auto mother = registerOk(*f.service, test::draft("Lakshmi Devi", "01-06-1975", "F", "9876543210"));

    // Child using the mother's number: same phone, different name/DOB.
    auto child = test::draft("Arjun", "14-08-2015", "M", "098765-43210");
    auto r = f.service->registerPatient(test::receptionist(), child, DuplicateDecision::none());
    REQUIRE(r.ok());
    REQUIRE(r.value().status == Status::NeedsDecision);
    REQUIRE(r.value().candidates.size() == 1);
    CHECK(r.value().candidates[0].patient.uhid == mother);
    CHECK(r.value().candidates[0].kind == CandidateKind::SamePhone);
    CHECK_FALSE(r.value().reasonRequired);
    CHECK(r.value().differentPersonAllowed);

    auto saved = f.service->registerPatient(test::receptionist(), child, differentPerson(r.value(), std::nullopt));
    REQUIRE(saved.ok());
    REQUIRE(saved.value().status == Status::Registered);
    CHECK(f.count("SELECT count(*) FROM patient WHERE phone = '9876543210'") == 2);  // (7) phone is not unique
    CHECK_FALSE(AuditLog(f.db(), f.clock).recent(1).at(0).reasonCode.has_value());
}

TEST_CASE("TC-Reg-02 (9) one review screen lists strong and same-phone candidates together; "
          "a strong candidate who also shares the phone is listed once, as strong") {
    StoreFixture f;
    const auto ravi = registerOk(*f.service, test::draft("Ravi Kumar", "05-03-1990", "M", "9876543210"));
    // Wife shares Ravi's phone: needs a phone-match decision.
    auto wifeDraft = test::draft("Sita Kumar", "02-02-1992", "F", "9876543210");
    auto w = f.service->registerPatient(test::receptionist(), wifeDraft, DuplicateDecision::none());
    REQUIRE(w.value().status == Status::NeedsDecision);
    auto wife = f.service->registerPatient(test::receptionist(), wifeDraft, differentPerson(w.value(), std::nullopt));
    REQUIRE(wife.value().status == Status::Registered);

    // A second "Ravi Kumar 05-03-1990 M" with the same phone: Ravi is strong (listed once), Sita is same-phone.
    auto d = test::draft("Ravi Kumar", "05-03-1990", "M", "9876543210");
    auto r = f.service->registerPatient(test::receptionist(), d, DuplicateDecision::none());
    REQUIRE(r.value().status == Status::NeedsDecision);
    REQUIRE(r.value().candidates.size() == 2);
    CHECK(r.value().candidates[0].patient.uhid == ravi);
    CHECK(r.value().candidates[0].kind == CandidateKind::Strong);
    CHECK(r.value().candidates[1].patient.uhid == wife.value().uhid);
    CHECK(r.value().candidates[1].kind == CandidateKind::SamePhone);
    CHECK(r.value().reasonRequired);  // reason needed once because a strong candidate is listed
}

TEST_CASE("TC-Reg-02 (8),(9) a candidate that appears after the review forces a fresh review") {
    StoreFixture f;
    registerOk(*f.service, test::draft("Ravi Kumar", "05-03-1990", "M"));
    auto d = test::draft("Ravi Kumar", "05-03-1990", "M", "9811111111");
    auto r = f.service->registerPatient(test::receptionist(), d, DuplicateDecision::none());
    REQUIRE(r.value().status == Status::NeedsDecision);
    auto decision = differentPerson(r.value(), DuplicateReason::Other);

    // Meanwhile another patient with the same phone is registered (e.g. at the same desk earlier).
    auto other = test::draft("Meena", "01-01-2000", "F", "9811111111");
    REQUIRE(f.service->registerPatient(test::receptionist(), other, DuplicateDecision::none()).value().status ==
            Status::Registered);

    auto again = f.service->registerPatient(test::receptionist(), d, decision);
    REQUIRE(again.ok());
    REQUIRE(again.value().status == Status::NeedsDecision);  // rolled back, review again
    REQUIRE(again.value().candidates.size() == 2);
    int newOnes = 0;
    for (const auto& c : again.value().candidates) newOnes += c.newSinceReview ? 1 : 0;
    CHECK(newOnes == 1);
    CHECK(f.count("SELECT count(*) FROM patient WHERE full_name = 'Ravi Kumar'") == 1);
}

TEST_CASE("TC-Reg-02 (6) UHIDs are never reused") {
    StoreFixture f;
    const auto a = registerOk(*f.service, test::draft("A Patient", "01-01-1990", "M"));
    // A failed registration (rolled back) must not consume or reuse a number visibly.
    f.db().setFaultHook([](const char* point) {
        if (std::string(point) == "register_before_commit") throw InjectedFault(point);
    });
    auto failed = f.service->registerPatient(test::receptionist(), test::draft("B Patient", "01-01-1991", "F"),
                                             DuplicateDecision::none());
    REQUIRE_FALSE(failed.ok());
    CHECK(failed.error().kind == ErrorKind::StorageFailure);
    f.db().setFaultHook(nullptr);
    const auto c = registerOk(*f.service, test::draft("C Patient", "01-01-1992", "F"));
    CHECK(c > a);
}

TEST_CASE("TC-Reg-02 (5) / NF-002 a failure before commit leaves neither patient nor audit entry") {
    StoreFixture f;
    for (const char* point : {"register_before_commit", "before_commit"}) {
        CAPTURE(point);
        f.db().setFaultHook([point](const char* p) {
            if (std::string(p) == point) throw InjectedFault(p);
        });
        auto r = f.service->registerPatient(test::receptionist(), test::draft("Ravi Kumar", "05-03-1990", "M"),
                                            DuplicateDecision::none());
        CHECK_FALSE(r.ok());
        CHECK(f.count("SELECT count(*) FROM patient") == 0);
        CHECK(f.count("SELECT count(*) FROM audit_entry") == 0);
    }
    f.db().setFaultHook(nullptr);
    CHECK(registerOk(*f.service, test::draft("Ravi Kumar", "05-03-1990", "M")) > 0);
}

TEST_CASE("TC-Reg-02 (4) audit entries contain IDs and codes, never names, phones or addresses") {
    StoreFixture f;
    registerOk(*f.service, test::draft("Ravi Kumar", "05-03-1990", "M", "9876543210", "Flat 4, Jayanagar"));
    auto d = test::draft("Ravi Kumar", "05-03-1990", "M", "9876543210", "Flat 4, Jayanagar");
    auto r = f.service->registerPatient(test::receptionist(), d, DuplicateDecision::none());
    REQUIRE(f.service->registerPatient(test::receptionist(), d, differentPerson(r.value(), DuplicateReason::Other))
                .value()
                .status == Status::Registered);
    for (const auto& e : AuditLog(f.db(), f.clock).recent(10)) {
        for (const auto& field : {e.recordId, e.changedFields, e.reasonCode}) {
            if (!field) continue;
            CHECK(field->find("Ravi") == std::string::npos);
            CHECK(field->find("9876543210") == std::string::npos);
            CHECK(field->find("Jayanagar") == std::string::npos);
        }
    }
}

TEST_CASE("SR-001 only a Receptionist may register; denials are audited and change nothing") {
    StoreFixture f;
    for (const auto& s : {test::doctor(), test::pharmacist()}) {
        CAPTURE(s.userId);
        auto r = f.service->registerPatient(s, test::draft("Ravi Kumar", "05-03-1990", "M"), DuplicateDecision::none());
        REQUIRE_FALSE(r.ok());
        CHECK(r.error().kind == ErrorKind::AccessDenied);
    }
    CHECK(f.count("SELECT count(*) FROM patient") == 0);
    CHECK(f.count("SELECT count(*) FROM audit_entry WHERE outcome = 'DENIED' AND action = 'PATIENT_REGISTER'") == 2);
}

TEST_CASE("Policy switch: SRS v1.1 behaviour (a matching phone blocks a new UHID)") {
    AppPolicy strict;
    strict.patient.phoneMatchAllowsNewPatient = false;
    StoreFixture f(strict);
    const auto mother = registerOk(*f.service, test::draft("Lakshmi Devi", "01-06-1975", "F", "9876543210"));
    auto child = test::draft("Arjun", "14-08-2015", "M", "9876543210");
    auto r = f.service->registerPatient(test::receptionist(), child, DuplicateDecision::none());
    REQUIRE(r.value().status == Status::NeedsDecision);
    CHECK_FALSE(r.value().differentPersonAllowed);
    auto refused = f.service->registerPatient(test::receptionist(), child, differentPerson(r.value(), std::nullopt));
    REQUIRE_FALSE(refused.ok());
    CHECK(refused.error().kind == ErrorKind::Conflict);
    CHECK(f.service->useExistingPatient(test::receptionist(), child, mother, {mother}).ok());
    CHECK(f.count("SELECT count(*) FROM patient") == 1);
}

TEST_CASE("Validation errors are returned before anything is written") {
    StoreFixture f;
    auto r = f.service->registerPatient(test::receptionist(), test::draft("", "31-02-2026", "X", "123"),
                                        DuplicateDecision::none());
    REQUIRE_FALSE(r.ok());
    CHECK(r.error().kind == ErrorKind::Validation);
    CHECK(r.error().fields.size() >= 4);
    CHECK(f.count("SELECT count(*) FROM patient") == 0);
    CHECK(f.count("SELECT count(*) FROM audit_entry") == 0);
}
