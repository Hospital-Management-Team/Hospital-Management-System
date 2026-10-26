
#include "doctest.h"

#include "auth/authentication.hpp"
#include "test_support.hpp"

using namespace hms;
using namespace hms::auth;

TEST_CASE("Staff account creation") {
    test::TempStore store;

    CHECK(createStaffAccount(
        store.db(), "doctor1", "SecurePass123!", Role::Doctor));

    CHECK_FALSE(createStaffAccount(
        store.db(), "doctor1", "AnotherPass123!", Role::Doctor));

    CHECK_FALSE(createStaffAccount(
        store.db(), "", "SecurePass123!", Role::Doctor));

    CHECK(store.count("SELECT COUNT(*) FROM staff_accounts") == 1);
}

TEST_CASE("Successful login returns the correct role") {
    test::TempStore store;

    REQUIRE(createStaffAccount(
        store.db(), "doctor1", "SecurePass123!", Role::Doctor));

    const auto result = login(store.db(), "doctor1", "SecurePass123!");

    CHECK(result.status == LoginStatus::Success);
    CHECK(result.role == Role::Doctor);
    CHECK(result.failedAttempts == 0);
}

TEST_CASE("Incorrect password increments failed attempts") {
    test::TempStore store;

    REQUIRE(createStaffAccount(
        store.db(), "doctor1", "SecurePass123!", Role::Doctor));

    const auto result = login(store.db(), "doctor1", "wrong-password");

    CHECK(result.status == LoginStatus::InvalidCredentials);
    CHECK(result.failedAttempts == 1);
    CHECK(store.count(
        "SELECT failed_attempts FROM staff_accounts WHERE username='doctor1'"
    ) == 1);
}

TEST_CASE("Account locks after five failed login attempts") {
    test::TempStore store;

    REQUIRE(createStaffAccount(
        store.db(), "doctor1", "SecurePass123!", Role::Doctor));

    for (int i = 0; i < 4; ++i) {
        const auto result = login(store.db(), "doctor1", "wrong-password");
        CHECK(result.status == LoginStatus::InvalidCredentials);
    }

    const auto fifth = login(store.db(), "doctor1", "wrong-password");

    CHECK(fifth.status == LoginStatus::AccountLocked);
    CHECK(fifth.failedAttempts == 5);

    const auto next = login(store.db(), "doctor1", "SecurePass123!");
    CHECK(next.status == LoginStatus::AccountLocked);
}

TEST_CASE("Successful login resets failed attempts") {
    test::TempStore store;

    REQUIRE(createStaffAccount(
        store.db(), "doctor1", "SecurePass123!", Role::Doctor));

    login(store.db(), "doctor1", "wrong-password");
    login(store.db(), "doctor1", "wrong-password");

    const auto result = login(store.db(), "doctor1", "SecurePass123!");

    CHECK(result.status == LoginStatus::Success);
    CHECK(result.failedAttempts == 0);
    CHECK(store.count(
        "SELECT failed_attempts FROM staff_accounts WHERE username='doctor1'"
    ) == 0);
}

TEST_CASE("Role-based permissions") {
    CHECK(isAuthorized(Role::Administrator, "manage_staff"));
    CHECK(isAuthorized(Role::Administrator, "register_patient"));
    CHECK_FALSE(isAuthorized(Role::Doctor, "manage_staff"));
    CHECK(isAuthorized(Role::Doctor, "view_patient_record"));
    CHECK(isAuthorized(Role::Receptionist, "register_patient"));
    CHECK_FALSE(isAuthorized(Role::Receptionist, "view_patient_record"));
}
TEST_CASE("Password policy") {
    std::string why;
    CHECK(isPasswordAcceptable("SecurePass123!", why));
    CHECK_FALSE(isPasswordAcceptable("Ab1!", why));              // too short
    CHECK_FALSE(isPasswordAcceptable("alllowercase1!", why));    // no upper
    CHECK_FALSE(isPasswordAcceptable("ALLUPPERCASE1!", why));    // no lower
    CHECK_FALSE(isPasswordAcceptable("NoDigitsHere!", why));     // no digit
    CHECK_FALSE(isPasswordAcceptable("NoSymbol12345", why));     // no symbol
}

TEST_CASE("Weak password is rejected at account creation") {
    test::TempStore store;
    CHECK_FALSE(createStaffAccount(store.db(), "doctor1", "weak", Role::Doctor));
    CHECK(store.count("SELECT COUNT(*) FROM staff_accounts") == 0);
}

TEST_CASE("Password is stored only as a salted Argon2id hash") {
    test::TempStore store;
    REQUIRE(createStaffAccount(store.db(), "a1", "SecurePass123!", Role::Doctor));
    REQUIRE(createStaffAccount(store.db(), "a2", "SecurePass123!", Role::Doctor));

    auto s1 = store.db().prepare("SELECT password_hash FROM staff_accounts WHERE username='a1'");
    REQUIRE(s1.step());
    const std::string h1 = s1.getText(0);
    auto s2 = store.db().prepare("SELECT password_hash FROM staff_accounts WHERE username='a2'");
    REQUIRE(s2.step());
    const std::string h2 = s2.getText(0);

    CHECK(h1.rfind("$argon2id$", 0) == 0);
    CHECK(h1.find("SecurePass123!") == std::string::npos);
    CHECK(h1 != h2);   // different salt, same password
}

TEST_CASE("Unknown user fails; usernames are case-insensitive") {
    test::TempStore store;
    CHECK_FALSE(hasStaffAccounts(store.db()));
    REQUIRE(createStaffAccount(store.db(), "Doctor1", "SecurePass123!", Role::Doctor));
    CHECK(hasStaffAccounts(store.db()));
    CHECK(login(store.db(), "nobody", "SecurePass123!").status == LoginStatus::InvalidCredentials);
    CHECK(login(store.db(), "doctor1", "SecurePass123!").status == LoginStatus::Success);
}