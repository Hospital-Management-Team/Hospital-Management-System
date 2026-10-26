
#include "auth/authentication.hpp"

#include <argon2.h>

#include <array>
#include <cerrno>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
#else
#include <sys/random.h>
#endif

namespace hms::auth {
namespace {

constexpr int MAX_FAILED_ATTEMPTS = 5;

std::string roleToString(Role role) {
    switch (role) {
        case Role::Administrator: return "administrator";
        case Role::Doctor: return "doctor";
        case Role::Receptionist: return "receptionist";
    }
    throw std::invalid_argument("Invalid staff role");
}

Role stringToRole(const std::string& role) {
    if (role == "administrator") return Role::Administrator;
    if (role == "doctor") return Role::Doctor;
    if (role == "receptionist") return Role::Receptionist;
    throw std::runtime_error("Invalid role stored in database");
}

void secureRandom(unsigned char* buffer, std::size_t length) {
#ifdef _WIN32
    if (BCryptGenRandom(
            nullptr, buffer, static_cast<ULONG>(length),
            BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) {
        throw std::runtime_error("Secure random generation failed");
    }
#else
    std::size_t offset = 0;
    while (offset < length) {
        const auto result = getrandom(
            buffer + offset, length - offset, 0);

        if (result < 0) {
            if (errno == EINTR) continue;
            throw std::runtime_error("Secure random generation failed");
        }
        offset += static_cast<std::size_t>(result);
    }
#endif
}

std::string hashPassword(const std::string& password) {
    std::array<unsigned char, 16> salt{};
    secureRandom(salt.data(), salt.size());

    std::vector<char> encoded(256);

    const int result = argon2id_hash_encoded(
        2, 19456, 1,
        password.data(), password.size(),
        salt.data(), salt.size(),
        32, encoded.data(), encoded.size());

    if (result != ARGON2_OK) {
        throw std::runtime_error("Password hashing failed");
    }

    return std::string(encoded.data());
}

bool verifyPassword(
    const std::string& password,
    const std::string& encodedHash) {
    return argon2id_verify(
        encodedHash.c_str(),
        password.data(),
        password.size()) == ARGON2_OK;
}

} // namespace

bool createStaffAccount(
    Database& db,
    const std::string& username,
    const std::string& password,
    Role role) {

    if (username.empty() || password.empty() ||
        username.find_first_of(" \t\r\n") != std::string::npos) {
        return false;
    }

    const std::string roleName = roleToString(role);
    const std::string passwordHash = hashPassword(password);

    try {
        Transaction tx(db);

        auto statement = db.prepare(
            "INSERT INTO staff_accounts "
            "(username, password_hash, role) VALUES (?, ?, ?)");

        statement.bind(1, username)
                 .bind(2, passwordHash)
                 .bind(3, roleName)
                 .run();

        tx.commit();
        return true;
    } catch (const DbError&) {
        return false;
    }
}

LoginResult login(
    Database& db,
    const std::string& username,
    const std::string& password) {

    Transaction tx(db);

    std::int64_t staffId = 0;
    std::string passwordHash;
    std::string storedRole;
    int failures = 0;
    bool locked = false;
    bool found = false;

    {
        auto statement = db.prepare(
            "SELECT staff_id, password_hash, role, "
            "failed_attempts, locked "
            "FROM staff_accounts WHERE username = ?");

        statement.bind(1, username);

        if (statement.step()) {
            found = true;
            staffId = statement.getInt64(0);
            passwordHash = statement.getText(1);
            storedRole = statement.getText(2);
            failures = static_cast<int>(statement.getInt64(3));
            locked = statement.getInt64(4) != 0;
        }
    } // Finalize the SELECT before updating the same table.

    if (!found) {
        tx.commit();
        return {LoginStatus::InvalidCredentials,
                Role::Receptionist, 0};
    }

    if (locked) {
        tx.commit();
        return {LoginStatus::AccountLocked,
                Role::Receptionist, failures};
    }

    if (!verifyPassword(password, passwordHash)) {
        const int updatedFailures = failures + 1;
        const bool shouldLock = updatedFailures >= MAX_FAILED_ATTEMPTS;

        auto update = db.prepare(
            "UPDATE staff_accounts "
            "SET failed_attempts = ?, locked = ? "
            "WHERE staff_id = ?");

        update.bind(1, updatedFailures)
              .bind(2, shouldLock ? 1 : 0)
              .bind(3, staffId)
              .run();

        tx.commit();

        return {
            shouldLock ? LoginStatus::AccountLocked
                       : LoginStatus::InvalidCredentials,
            Role::Receptionist,
            updatedFailures
        };
    }

    const Role role = stringToRole(storedRole);

    auto reset = db.prepare(
        "UPDATE staff_accounts SET failed_attempts = 0 "
        "WHERE staff_id = ?");

    reset.bind(1, staffId).run();
    tx.commit();

    return {LoginStatus::Success, role, 0};
}

bool isAuthorized(Role role, const std::string& action) {
    if (role == Role::Administrator) {
        return action == "manage_staff" ||
               action == "register_patient" ||
               action == "search_patients" ||
               action == "view_patient_record";
    }

    if (role == Role::Doctor) {
        return action == "search_patients" ||
               action == "view_patient_record";
    }

    if (role == Role::Receptionist) {
        return action == "register_patient" ||
               action == "search_patients";
    }

    return false;
}

} // namespace hms::auth
