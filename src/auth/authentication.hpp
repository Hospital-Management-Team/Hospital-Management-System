#pragma once

#include <string>
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

bool createStaffAccount(
    Database& db,
    const std::string& username,
    const std::string& password,
    Role role
);

LoginResult login(
    Database& db,
    const std::string& username,
    const std::string& password
);

bool isAuthorized(Role role, const std::string& action);

// Password policy (SRS HMS-SR-002): 8-128 chars with upper, lower, digit, symbol.
bool isPasswordAcceptable(const std::string& password, std::string& reason);

// True if at least one staff account exists (used for first-run setup).
bool hasStaffAccounts(Database& db);
}