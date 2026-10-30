
#include "auth/authentication.hpp"
#include <chrono>
#include <optional>

#include "audit/audit_log.hpp"
#include <argon2.h>
#include <cctype>
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
bool isPasswordAcceptable(const std::string& password, std::string& reason) {
    if (password.size() < 8 || password.size() > 128) {
        reason = "Password must be 8 to 128 characters long.";
        return false;
    }
    bool upper = false, lower = false, digit = false, symbol = false;
    for (const char ch : password) {
        const auto c = static_cast<unsigned char>(ch);
        if (std::isupper(c)) upper = true;
        else if (std::islower(c)) lower = true;
        else if (std::isdigit(c)) digit = true;
        else symbol = true;
    }
    if (!(upper && lower && digit && symbol)) {
        reason = "Password needs an upper-case letter, a lower-case letter, a digit and a symbol.";
        return false;
    }
    return true;
}

bool hasStaffAccounts(Database& db) {
    auto statement = db.prepare("SELECT 1 FROM staff_accounts LIMIT 1");
    return statement.step();
}
const Clock& defaultClock() {
    static const SystemClock clock;
    return clock;
}

bool createStaffAccount(
    Database& db,
    const std::string& username,
    const std::string& password,
    Role role,
    const Clock& clock,
    const std::string& actorId) {

    std::string reason;
    if (username.empty() ||
        username.find_first_of(" \t\r\n") != std::string::npos ||
        !isPasswordAcceptable(password, reason)) {
        return false;
    }

    const std::string roleName = roleToString(role);
    const std::string passwordHash = hashPassword(password);

    try {
        Transaction tx(db);

        {
            auto statement = db.prepare(
                "INSERT INTO staff_accounts "
                "(username, password_hash, role) VALUES (?, ?, ?)");
            statement.bind(1, username)
                .bind(2, passwordHash)
                .bind(3, roleName)
                .run();
        }

        std::string staffId;
        {
            auto statement = db.prepare(
                "SELECT staff_id FROM staff_accounts WHERE username = ?");
            statement.bind(1, username);
            if (statement.step()) staffId = std::to_string(statement.getInt64(0));
        }

        AuditLog audit(db, clock);
        AuditEntryInput entry;
        entry.action = audit_action::kStaffAccountCreate;
        entry.recordId = staffId;
        entry.changedFields = {"username", "password_hash", "role"};  // names only
        audit.append(actorId, entry);

        tx.commit();
        return true;
    } catch (const DbError&) {
        return false;
    }
}

LoginResult login(
    Database& db,
    const std::string& username,
    const std::string& password,
    const Clock& clock,
    const AuthPolicy& policy) {

    AuditLog audit(db, clock);
    Transaction tx(db);
    const std::string now = toIsoUtc(clock.now());

    // Entries hold the staff ID only; text typed at the login screen is never stored.
    auto record = [&](const std::string& user, const std::optional<std::string>& recordId,
                      AuditOutcome outcome, const char* reasonCode, const char* action,
                      const std::optional<std::string>& related = std::nullopt) {
        AuditEntryInput entry;
        entry.action = action;
        entry.recordId = recordId;
        entry.outcome = outcome;
        if (reasonCode) entry.reasonCode = reasonCode;
        entry.relatedEventId = related;
        return audit.append(user, entry);
    };

    std::int64_t staffId = 0;
    std::string storedName;
    std::string passwordHash;
    std::string storedRole;
    std::string lockedUntil;
    int failures = 0;
    bool found = false;

    {
        auto statement = db.prepare(
            "SELECT staff_id, username, password_hash, role, "
            "failed_attempts, COALESCE(locked_until, '') "
            "FROM staff_accounts WHERE username = ?");

        statement.bind(1, username);

        if (statement.step()) {
            found = true;
            staffId = statement.getInt64(0);
            storedName = statement.getText(1);
            passwordHash = statement.getText(2);
            storedRole = statement.getText(3);
            failures = static_cast<int>(statement.getInt64(4));
            lockedUntil = statement.getText(5);
        }
    }  // Finalize the SELECT before updating the same table.

    if (!found) {
        record("UNKNOWN", std::nullopt, AuditOutcome::Denied, "UNKNOWN_USER", audit_action::kStaffLogin);
        tx.commit();
        return {LoginStatus::InvalidCredentials, Role::Receptionist, 0};
    }

    const std::string staffKey = std::to_string(staffId);

    if (!lockedUntil.empty() && lockedUntil > now) {
        record(storedName, staffKey, AuditOutcome::Denied, "ACCOUNT_LOCKED", audit_action::kStaffLogin);
        tx.commit();
        return {LoginStatus::AccountLocked, Role::Receptionist, failures};
    }
    if (!lockedUntil.empty()) failures = 0;  // the cooldown is over: start counting again

    if (!verifyPassword(password, passwordHash)) {
        const int updatedFailures = failures + 1;
        const bool shouldLock = updatedFailures >= policy.maxFailedAttempts;
        std::optional<std::string> until;
        if (shouldLock) {
            until = toIsoUtc(clock.now() + std::chrono::minutes(policy.lockoutMinutes));
        }

        auto update = db.prepare(
            "UPDATE staff_accounts "
            "SET failed_attempts = ?, locked = ?, locked_until = ? "
            "WHERE staff_id = ?");

        update.bind(1, updatedFailures)
            .bind(2, shouldLock ? 1 : 0)
            .bind(3, until)
            .bind(4, staffId)
            .run();

        const std::string failedId = record(storedName, staffKey, AuditOutcome::Denied,
                                            "BAD_PASSWORD", audit_action::kStaffLogin);
        if (shouldLock) {
            record(storedName, staffKey, AuditOutcome::Ok, "MAX_FAILED_ATTEMPTS",
                   audit_action::kStaffLockout, failedId);
        }
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
        "UPDATE staff_accounts SET failed_attempts = 0, locked = 0, locked_until = NULL "
        "WHERE staff_id = ?");
    reset.bind(1, staffId).run();

    record(storedName, staffKey, AuditOutcome::Ok, nullptr, audit_action::kStaffLogin);
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
