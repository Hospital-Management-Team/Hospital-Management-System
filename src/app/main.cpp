// Hospital Management System - console application entry point.
//
// Usage:  hms [--store PATH]
//   --store PATH   data store file (default: database/hospital.db)

#include <filesystem>
#include <iostream>
#include <string>
#include <system_error>

#include "config/policy.hpp"
#include "core/clock.hpp"
#include "data/schema.hpp"
#include "data/store.hpp"
#include "platform/console_setup.hpp"
#include "patient/patient_service.hpp"
#include "platform/store_lock.hpp"
#include "ui/console_app.hpp"
#include "ui/console_io.hpp"

using namespace hms;

namespace {

void printUsage() {
    std::cout << "Usage: hms [--store PATH]\n"
                 "  --store PATH   data store file (default: database/hospital.db)\n"
                 "  --help         show this help\n"
                 "  --version      show the version\n";
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

    // 3. Session. Staff login is issue #3; until it is merged the user picks
    //    the role to simulate (deviation D-01). The business modules still
    //    check the role on every operation.
    out << "\nStaff login is not built yet (issue #3). Choose the role to simulate:\n"
           "  1. Receptionist\n  2. Doctor\n  0. Exit\n";
    Session session;
    session.simulated = true;
    while (true) {
        auto choice = io.readLine("Role: ");
        if (!choice || *choice == "0") {
            out << "Goodbye.\n";
            return 0;
        }
        if (*choice == "1") {
            session.role = Role::Receptionist;
            session.userId = "SIM-RECEPTION";
            session.displayName = "Reception desk (simulated)";
            break;
        }
        if (*choice == "2") {
            session.role = Role::Doctor;
            session.userId = "SIM-DOCTOR";
            session.displayName = "Doctor (simulated)";
            break;
        }
        out << "  Please choose 0, 1 or 2.\n";
    }

    SystemClock clock;
    AppPolicy policy;
    PatientService patients(*store.db, clock, policy);
    ConsoleApp app(io, patients, session);
    return app.run();
}
