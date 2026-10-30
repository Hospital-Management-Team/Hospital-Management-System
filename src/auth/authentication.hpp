#pragma once

#include <string>
#include "config/policy.hpp"
#include "core/clock.hpp"
#include "data/database.hpp"

namespace hms::auth {

enum class LoginStatus {
    Success,
    InvalidCredentials,
    AccountLocked
};

enum class Role {
    Administrator,
    Doctor,
    Receptionist
};

struct LoginResult {
    LoginStatus status;
    Role role;
    int failedAttempts;
};

// The clock and policy default to the real clock and the standard values, so
// existing callers keep working; tests pass a FixedClock.
const Clock& defaultClock();

// actorId is the staff account that creates the account ("SETUP" for the
// first Administrator). One STAFF_ACCOUNT_CREATE audit entry is written.
bool createStaffAccount(
    Database& db,
    const std::string& username,
    const std::string& password,
    Role role,
    const Clock& clock = defaultClock(),
    const std::string& actorId = "SETUP"
);

// Every attempt writes a STAFF_LOGIN audit entry; the failure that locks the
// account also writes STAFF_LOCKOUT. The lock ends after policy.lockoutMinutes.
LoginResult login(
    Database& db,
    const std::string& username,
    const std::string& password,
    const Clock& clock = defaultClock(),
    const AuthPolicy& policy = AuthPolicy{}
);

bool isAuthorized(Role role, const std::string& action);

// Password policy (SRS HMS-SR-002): 8-128 chars with upper, lower, digit, symbol.
bool isPasswordAcceptable(const std::string& password, std::string& reason);

// True if at least one staff account exists (used for first-run setup).
bool hasStaffAccounts(Database& db);
}