// Hospital Management System - console application entry point.
//
// Usage:  hms [--store PATH]
//   --store PATH   data store file (default: database/hospital.db)

#include <filesystem>
#include <iostream>
#include <string>
#include <system_error>

#include "appointment/appointment_service.hpp"
#include "admin/admin_service.hpp"
#include "audit/audit_log_service.hpp"
#include "auth/authentication.hpp"
#include "auth/session.hpp"
#include "config/policy.hpp"
#include "config/settings.hpp"
#include "core/clock.hpp"
#include "data/schema.hpp"
#include "data/store.hpp"
#include "patient/patient_service.hpp"
#include "platform/console_setup.hpp"
#include "platform/password_input.hpp"
#include "platform/store_lock.hpp"
#include "billing/billing_service.hpp"
#include "lab/lab_service.hpp"
#include "pharmacy/pharmacy_service.hpp"
#include "ui/lab_pharmacy_screens.hpp"
#include "consultation/consultation_service.hpp"
#include "record/medical_record_service.hpp"
#include "ui/admin_screens.hpp"
#include "ui/audit_log_screen.hpp"
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
        case auth::Role::LabTechnician: return Role::LabTechnician;
        case auth::Role::Pharmacist: return Role::Pharmacist;
    }
    return Role::Receptionist;
}

const char* roleName(auth::Role r) {
    switch (r) {
        case auth::Role::Administrator: return "Administrator";
        case auth::Role::Doctor: return "Doctor";
        case auth::Role::Receptionist: return "Receptionist";
        case auth::Role::LabTechnician: return "Lab Technician";
        case auth::Role::Pharmacist: return "Pharmacist";
    }
    return "";
}

// The Admin session is used by the schedule screen, whose service checks the
// role and audits the change (issue #4). Account creation does not take it yet
// (deviation log D-17, D-22).
void adminMenu(ConsoleIO& io, Database& db, AppointmentService& appointments, BillingService& billing,
               AuditLogService& auditLog, AdminService& admin, const Session& session) {
    auto& out = io.out();
    while (true) {
        out << "\n--- ADMIN MENU ---\n"
               "  1. Create staff account\n"
               "  2. Set a doctor's department and weekly hours\n"
               "  3. Set a doctor's consultation fee\n"
               "  4. View the audit log (read only)\n"
               "  5. Manage staff accounts (deactivate, unlock, new password)\n"
               "  6. Summary reports\n"
               "  7. Master data (departments, rooms, doctor mapping)\n"
               "  8. System settings (appointment cutoff, lockout cooldown, low-stock threshold)\n"
               "  0. Log out\n";
        auto c = io.readLine("Choose an option: ");
        if (!c || *c == "0") return;
        if (*c == "2") { doctorScheduleScreen(io, appointments, session); continue; }
        if (*c == "3") { consultationFeeScreen(io, billing, session); continue; }
        if (*c == "4") { auditLogScreen(io, auditLog, session); continue; }
        if (*c == "5") { staffAccountsScreen(io, admin, session, readPassword); continue; }
        if (*c == "6") { reportsScreen(io, admin, session); continue; }
        if (*c == "7") { masterDataScreen(io, admin, session); continue; }
        if (*c == "8") { settingsScreen(io, admin, session); continue; }
        if (*c != "1") { out << "  Please choose a number from 0 to 8.\n"; continue; }

        auto u = io.readLine("New username: ");
        if (!u) return;
        out << "  Role: 1. Doctor   2. Receptionist   3. Administrator   4. Lab Technician   5. Pharmacist\n";
        auto r = io.readLine("Role: ");
        if (!r) return;

        auth::Role role = auth::Role::Doctor;
        if (*r == "1") role = auth::Role::Doctor;
        else if (*r == "2") role = auth::Role::Receptionist;
        else if (*r == "3") role = auth::Role::Administrator;
        else if (*r == "4") role = auth::Role::LabTechnician;
        else if (*r == "5") role = auth::Role::Pharmacist;
        else { out << "  Please choose 1, 2, 3, 4 or 5.\n"; continue; }

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

// Asks twice for a new store passphrase (not shown while typing). False if the input ended.
bool askNewPassphrase(ConsoleIO& io, std::string& passphrase) {
    auto& out = io.out();
    out << "\nChoose a passphrase for the data store (8 to 128 characters). It is asked at every start-up.\n"
           "It cannot be recovered: if it is lost, the data cannot be read.\n";
    while (true) {
        std::string first, second;
        if (!readPassword("New data store passphrase: ", first)) return false;
        if (!readPassword("Repeat the passphrase: ", second)) {
            wipe(first);
            return false;
        }
        std::string reason;
        const bool matches = first == second;
        const bool acceptable = matches && isStorePassphraseAcceptable(first, reason);
        if (acceptable) passphrase = first;
        wipe(first);
        wipe(second);
        if (acceptable) return true;
        out << "  " << (matches ? reason : std::string("The two passphrases do not match.")) << "\n";
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
    // A folder the application creates is limited to the current user, so the store's temporary
    // files are protected too (HMS-SR-005). A folder that already existed is left as it is.
    {
        std::error_code ec;
        const auto parent = std::filesystem::path(canonical).parent_path();
        if (!parent.empty() && !std::filesystem::exists(parent, ec)) {
            if (std::filesystem::create_directories(parent, ec)) {
                std::string aclError;
                std::string parentUtf8;
                {
                    const auto u8 = parent.u8string();
                    parentUtf8.assign(u8.begin(), u8.end());
                }
                if (!restrictToCurrentUser(parentUtf8, true, aclError)) {
                    out << "Warning: " << aclError << ".\n";
                }
            }
        }
    }

    // 1. One instance per data store (SRS 2.4): take the lock before opening.
    std::string lockError;
    auto lock = StoreLock::acquire(canonical, lockError);
    if (!lock) {
        out << "Cannot start: " << lockError << ".\nNothing was changed.\n";
        return 1;
    }
    // An existing store and its lock file are limited to the current user as well (HMS-SR-005).
    {
        std::string aclError;
        if (storeFileExists(canonical) && !restrictToCurrentUser(canonical, false, aclError)) {
            out << "Warning: " << aclError << ".\n";
        }
        if (!restrictToCurrentUser(lock->lockPath(), false, aclError)) out << "Warning: " << aclError << ".\n";
    }

    // 2. A missing store is created only after confirmation (HMS-F-053 rule 1).
    //    The store is always encrypted (HMS-NF-003): a new one gets a passphrase, an old plain one is
    //    encrypted once, and an encrypted one is opened with its passphrase.
    bool create = false;
    bool encryptPlain = false;
    std::string passphrase;
    const bool existing = storeFileExists(canonical);
    if (!existing) {
        out << "\nNo existing data store was found at:\n  " << canonical << "\n";
        auto yes = io.askYesNo("Set up a NEW, EMPTY data store here? (Y/N): ");
        if (!yes || !*yes) {
            out << "Nothing was created. If you expected existing data, check the --store path.\n";
            return 0;
        }
        create = true;
        if (!askNewPassphrase(io, passphrase)) return 0;
    } else if (storeFileIsPlain(canonical)) {
        out << "\nThis data store is NOT encrypted:\n  " << canonical << "\n"
            << "Patient data must not be kept in plain text. It will be encrypted in place with a passphrase you "
               "choose.\n"
               "If you want a backup first, copy the file now; a copy stays readable by anyone until you delete it.\n";
        auto yes = io.askYesNo("Encrypt this data store now? (Y/N): ");
        if (!yes || !*yes) {
            out << "Nothing was changed. The application does not run on a plain data store.\n";
            return 0;
        }
        encryptPlain = true;
        if (!askNewPassphrase(io, passphrase)) return 0;
    }

    StoreOpenResult store;
    if (existing && !encryptPlain) {
        for (int attempt = 0; attempt < 3; ++attempt) {
            if (!readPassword("Data store passphrase: ", passphrase)) return 0;
            store = openStore(canonical, false, passphrase);
            wipe(passphrase);
            if (store.status != StoreStatus::WrongPassphrase) break;
            out << "  The passphrase is wrong, or the data store is damaged.\n";
        }
    } else {
        store = openStore(canonical, create, passphrase, encryptPlain);
        wipe(passphrase);
    }
    if (store.status != StoreStatus::Opened) {
        out << "\nCannot start: " << store.message << "\n";
        return 1;
    }
    if (store.encryptedNow) {
        out << "The data store is now encrypted. Keep the passphrase safe: it cannot be recovered.\n";
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
    MedicalRecordService medicalRecords(*store.db, clock, policy);
    AuditLogService auditLog(*store.db, clock, policy);
    AdminService admin(*store.db, clock, policy);
    LabService lab(*store.db, clock, policy);
    PharmacyService pharmacy(*store.db, clock, policy);

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
                    << Settings(*store.db, policy).lockoutMinutes() << " minutes.\n";
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
            adminMenu(io, *store.db, appointments, billing, auditLog, admin, session);
        } else if (result.role == auth::Role::LabTechnician) {
            labMenu(io, lab, session);
        } else if (result.role == auth::Role::Pharmacist) {
            pharmacyMenu(io, pharmacy, session);
        } else {
            ConsoleApp app(io, patients, appointments, session);
            app.setConsultationService(consultations);
            app.setBillingService(billing);
            app.setMedicalRecordService(medicalRecords);
            app.setLabService(lab);
            app.run();
        }
        out << "Logged out.\n";
    }
}