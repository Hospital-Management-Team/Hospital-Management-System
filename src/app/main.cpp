// Hospital Management System - console application entry point.
//
// Usage:  hms [--store PATH]
//   --store PATH   data store file (default: database/hospital.db)

#include <filesystem>
#include <iostream>
#include <string>
#include <system_error>

#include "appointment/appointment_service.hpp"
#include "auth/authentication.hpp"
#include "auth/session.hpp"
#include "config/policy.hpp"
#include "core/clock.hpp"
#include "data/schema.hpp"
#include "data/store.hpp"
#include "patient/patient_service.hpp"
#include "platform/console_setup.hpp"
#include "platform/password_input.hpp"
#include "platform/store_lock.hpp"
#include "billing/billing_service.hpp"
#include "consultation/consultation_service.hpp"
#include "ui/billing_screens.hpp"
#include "ui/console_app.hpp"
#include "ui/console_io.hpp"
#include "ui/doctor_schedule_screen.hpp"

using namespace hms;

namespace {

void printUsage() {
    std::cout << "Usage: hms [--store PATH]\n"
                 "  --store PATH   data store file (default: database/hospital.db)\n"
                 "  --help         show this help\n"
                 "  --version      show the version\n";
}

Role toSessionRole(auth::Role r) {
    switch (r) {
        case auth::Role::Administrator: return Role::Admin;
        case auth::Role::Doctor: return Role::Doctor;
        case auth::Role::Receptionist: return Role::Receptionist;
    }
    return Role::Receptionist;
}

const char* roleName(auth::Role r) {
    switch (r) {
        case auth::Role::Administrator: return "Administrator";
        case auth::Role::Doctor: return "Doctor";
        case auth::Role::Receptionist: return "Receptionist";
    }
    return "";
}

// The Admin session is used by the schedule screen, whose service checks the
// role and audits the change (issue #4). Account creation does not take it yet
// (deviation log D-17, D-22).
void adminMenu(ConsoleIO& io, Database& db, AppointmentService& appointments, BillingService& billing,
               const Session& session) {
    auto& out = io.out();
    while (true) {
        out << "\n--- ADMIN MENU ---\n"
               "  1. Create staff account\n"
               "  2. Set a doctor's department and weekly hours\n"
               "  3. Set a doctor's consultation fee\n"
               "  0. Log out\n";
        auto c = io.readLine("Choose an option: ");
        if (!c || *c == "0") return;
        if (*c == "2") { doctorScheduleScreen(io, appointments, session); continue; }
        if (*c == "3") { consultationFeeScreen(io, billing, session); continue; }
        if (*c != "1") { out << "  Please choose 0, 1, 2 or 3.\n"; continue; }

        auto u = io.readLine("New username: ");
        if (!u) return;
        out << "  Role: 1. Doctor   2. Receptionist   3. Administrator\n";
        auto r = io.readLine("Role: ");
        if (!r) return;

        auth::Role role = auth::Role::Doctor;
        if (*r == "1") role = auth::Role::Doctor;
        else if (*r == "2") role = auth::Role::Receptionist;
        else if (*r == "3") role = auth::Role::Administrator;
        else { out << "  Please choose 1, 2 or 3.\n"; continue; }

        std::string p1, p2;
        if (!readPassword("Password: ", p1)) return;
        if (!readPassword("Repeat password: ", p2)) return;
        if (p1 != p2) { out << "  Passwords do not match.\n"; continue; }

        std::string reason;
        if (!auth::isPasswordAcceptable(p1, reason)) { out << "  " << reason << "\n"; continue; }

        if (auth::createStaffAccount(db, *u, p1, role, auth::defaultClock(), session.userId)) {
            out << "  Account created for '" << *u << "'.\n";
        } else {
            out << "  Could not create the account (empty name, contains spaces, or already used).\n";
        }
    }
}

}  // namespace

int main(int argc, char** argv) {
    setUpConsoleUtf8();

    std::string storePath = "database/hospital.db";
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            printUsage();
            return 0;
        }
        if (arg == "--version") {
            std::cout << "HMS 0.1.0, schema version " << schema::currentVersion() << "\n";
            return 0;
        }
        if (arg == "--store" && i + 1 < argc) {
            storePath = argv[++i];
            continue;
        }
        std::cerr << "Unknown option: " << arg << "\n";
        printUsage();
        return 2;
    }

    ConsoleIO io(std::cin, std::cout);
    auto& out = io.out();
    out << "==============================================================================\n"
           " HOSPITAL MANAGEMENT SYSTEM  -  Patient records and appointments\n"
           "==============================================================================\n";

    // The folder that holds the store is created if it is missing.
    const std::string canonical = canonicalStorePath(storePath);
    {
        std::error_code ec;
        const auto parent = std::filesystem::path(canonical).parent_path();
        if (!parent.empty()) std::filesystem::create_directories(parent, ec);
    }

    // 1. One instance per data store (SRS 2.4): take the lock before opening.
    std::string lockError;
    auto lock = StoreLock::acquire(canonical, lockError);
    if (!lock) {
        out << "Cannot start: " << lockError << ".\nNothing was changed.\n";
        return 1;
    }

    // 2. A missing store is created only after confirmation (HMS-F-053 rule 1).
    bool create = false;
    if (!storeFileExists(canonical)) {
        out << "\nNo existing data store was found at:\n  " << canonical << "\n";
        auto yes = io.askYesNo("Set up a NEW, EMPTY data store here? (Y/N): ");
        if (!yes || !*yes) {
            out << "Nothing was created. If you expected existing data, check the --store path.\n";
            return 0;
        }
        create = true;
    }

    auto store = openStore(canonical, create);
    if (store.status != StoreStatus::Opened) {
        out << "\nCannot start: " << store.message << "\n";
        return 1;
    }
    out << "\nData store: " << canonical << (store.created ? "  (new)" : "") << "\n";
    if (store.migrated) out << "The data store was upgraded to schema version " << store.schemaVersion << ".\n";

    // 3. First run: no staff accounts yet -> create the first Administrator.
    if (!auth::hasStaffAccounts(*store.db)) {
        out << "\n=== First-time setup: create the Administrator account ===\n"
               "Password: 8+ characters with upper-case, lower-case, a digit and a symbol.\n";
        while (true) {
            auto u = io.readLine("Admin username: ");
            if (!u) return 0;
            std::string p1, p2;
            if (!readPassword("Password: ", p1)) return 0;
            if (!readPassword("Repeat password: ", p2)) return 0;
            if (p1 != p2) { out << "  Passwords do not match.\n"; continue; }
            std::string reason;
            if (!auth::isPasswordAcceptable(p1, reason)) { out << "  " << reason << "\n"; continue; }
            if (!auth::createStaffAccount(*store.db, *u, p1, auth::Role::Administrator)) {
                out << "  Could not create the account (empty name, spaces, or already used).\n";
                continue;
            }
            out << "  Administrator account created.\n";
            break;
        }
    }

    // 4. Staff login. The role comes from the stored account, never from the user.
    SystemClock clock;
    AppPolicy policy;
    PatientService patients(*store.db, clock, policy);
    AppointmentService appointments(*store.db, clock, policy);
    ConsultationService consultations(*store.db, clock, policy);
    BillingService billing(*store.db, clock, policy);

    while (true) {
        out << "\n=== Staff Login ===   (Ctrl+Z then Enter to quit)\n";
        bool loggedIn = false;
        bool locked = false;
        std::string username;
        auth::LoginResult result{auth::LoginStatus::InvalidCredentials,
                                 auth::Role::Receptionist, 0};

        for (int attempt = 0; attempt < 5 && !loggedIn && !locked; ++attempt) {
            auto u = io.readLine("Username: ");
            if (!u) return 0;
            std::string pw;
            if (!readPassword("Password: ", pw)) return 0;
            result = auth::login(*store.db, *u, pw, clock, policy.auth);
            if (result.status == auth::LoginStatus::Success) {
                loggedIn = true;
                username = *u;
            } else if (result.status == auth::LoginStatus::AccountLocked) {
                locked = true;
                out << "\nThis account is locked after repeated failed attempts. Try again in "
                    << policy.auth.lockoutMinutes << " minutes.\n";
            } else {
                out << "\nInvalid username or password.\n";
            }
        }

        if (!loggedIn) {
            if (!locked) {
                out << "Too many failed attempts. Exiting.\n";
                return 0;
            }
            continue;  // locked: back to the login screen
        }

        out << "\nLogin successful. Role: " << roleName(result.role) << "\n";

        Session session;
        session.userId = username;
        session.displayName = username;
        session.role = toSessionRole(result.role);
        session.simulated = false;
        if (result.role == auth::Role::Administrator) {
            adminMenu(io, *store.db, appointments, billing, session);
        } else {
            ConsoleApp app(io, patients, appointments, session);
            app.setConsultationService(consultations);
            app.setBillingService(billing);
            app.run();
        }
        out << "Logged out.\n";
    }
}