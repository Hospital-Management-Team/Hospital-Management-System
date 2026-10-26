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

}