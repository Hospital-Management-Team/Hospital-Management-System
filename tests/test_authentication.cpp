
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
