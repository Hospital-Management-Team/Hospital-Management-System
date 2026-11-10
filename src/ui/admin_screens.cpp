#include "ui/admin_screens.hpp"

#include <optional>
#include <ostream>

#include "core/money.hpp"
#include "core/text.hpp"
#include "validation/input.hpp"

namespace hms {

namespace {
std::string displayDate(const std::string& iso) {
    auto d = Date::fromIso(iso);
    return d ? d->toDisplay() : iso;
}

std::string statusText(const StaffMember& m) {
    if (!m.active) return "deactivated";
    if (m.locked) return "locked";
    return "active";
}

std::string money(std::int64_t paise) { return formatRupees(paise); }
}  // namespace

void staffAccountsScreen(ConsoleIO& io, AdminService& admin, const Session& session, const PasswordReader& readPassword) {
    auto& out = io.out();
    while (true) {
        auto list = admin.listStaff(session);
        if (!list) { io.showError(list.error()); return; }
        const auto& rows = list.value();
        out << "\nSTAFF ACCOUNTS\n  #   " << text::fitWidth("Username", 20) << text::fitWidth("Role", 16) << "Status\n";
        for (std::size_t i = 0; i < rows.size(); ++i) {
            out << "  " << text::fitWidth(std::to_string(i + 1) + ".", 4) << text::fitWidth(rows[i].username, 20)
                << text::fitWidth(rows[i].role, 16) << statusText(rows[i]) << "\n";
        }
        auto pick = io.readLine("Account to change (row number, Enter to go back): ");
        if (!pick || text::trimSpaces(*pick).empty()) return;
        auto n = input::parseWholeNumber(text::trimSpaces(*pick), "Account", 1, static_cast<std::int64_t>(rows.size()));
        if (!n) { io.showError(n.error()); continue; }
        const StaffMember& m = rows[static_cast<std::size_t>(n.value() - 1)];

        out << "\n" << m.username << " (" << m.role << ", " << statusText(m) << ")\n"
            << "  1. " << (m.active ? "Deactivate" : "Reactivate") << " the account\n"
            << "  2. Unlock the account\n"
            << "  3. Set a new password\n"
            << "  0. Back\n";
        auto c = io.readLine("Choose an option: ");
        if (!c) return;
        const std::string choice = text::trimSpaces(*c);
        if (choice == "0" || choice.empty()) continue;
        if (choice == "1") {
            auto sure = io.askYesNo(std::string(m.active ? "Deactivate " : "Reactivate ") + m.username + "? (y/n): ");
            if (!sure) return;
            if (!*sure) { out << "  Nothing changed.\n"; continue; }
            auto r = m.active ? admin.deactivate(session, m.staffId) : admin.reactivate(session, m.staffId);
            if (!r) io.showError(r.error());
            else out << "  " << m.username << (m.active ? " is deactivated and can no longer log in.\n" : " is active again.\n");
        } else if (choice == "2") {
            auto r = admin.unlock(session, m.staffId);
            if (!r) io.showError(r.error());
            else out << "  " << m.username << " is unlocked.\n";
        } else if (choice == "3") {
            std::string p1, p2;
            if (!readPassword("New password: ", p1)) return;
            if (!readPassword("Repeat new password: ", p2)) return;
            if (p1 != p2) { out << "  Passwords do not match.\n"; continue; }
            auto r = admin.resetPassword(session, m.staffId, p1);
            if (!r) io.showError(r.error());
            else out << "  The password for " << m.username << " is changed.\n";
        } else {
            out << "  Please choose 0, 1, 2 or 3.\n";
        }
    }
}

namespace {
// Reads a row number from a list of `size` rows; nullopt on Enter or end of input.
std::optional<std::size_t> pickRow(ConsoleIO& io, const std::string& prompt, std::size_t size) {
    while (true) {
        auto line = io.readLine(prompt);
        if (!line || text::trimSpaces(*line).empty()) return std::nullopt;
        auto n = input::parseWholeNumber(text::trimSpaces(*line), "Row", 1, static_cast<std::int64_t>(size));
        if (n) return static_cast<std::size_t>(n.value() - 1);
        io.showError(n.error());
    }
}

void departmentsScreen(ConsoleIO& io, AdminService& admin, const Session& session) {
    auto& out = io.out();
    while (true) {
        auto list = admin.listDepartments(session);
        if (!list) { io.showError(list.error()); return; }
        const auto& rows = list.value();
        out << "\nDEPARTMENTS\n  #   " << text::fitWidth("Name", 28) << text::fitWidth("Doctors", 9) << "Status\n";
        for (std::size_t i = 0; i < rows.size(); ++i) {
            out << "  " << text::fitWidth(std::to_string(i + 1) + ".", 4) << text::fitWidth(rows[i].name, 28)
                << text::fitWidth(std::to_string(rows[i].doctors), 9) << (rows[i].active ? "in use" : "switched off") << "\n";
        }
        out << "  A. Add a department   R. Rename one   S. Switch one off or on   Enter. Back\n";
        auto c = io.readLine("Choose: ");
        if (!c) return;
        const std::string choice = text::asciiLower(text::trimSpaces(*c));
        if (choice.empty()) return;
        if (choice == "a") {
            auto name = io.readLine("New department name: ");
            if (!name) return;
            auto r = admin.addDepartment(session, *name);
            if (!r) io.showError(r.error()); else out << "  Department added.\n";
        } else if (choice == "r" || choice == "s") {
            if (rows.empty()) { out << "  There are no departments yet.\n"; continue; }
            auto row = pickRow(io, "Department (row number): ", rows.size());
            if (!row) continue;
            const Department& d = rows[*row];
            if (choice == "r") {
                auto name = io.readLine("New name for " + d.name + ": ");
                if (!name) return;
                auto r = admin.renameDepartment(session, d.departmentId, *name);
                if (!r) io.showError(r.error()); else out << "  Renamed. Booking and billing show the new name now.\n";
            } else {
                auto r = admin.setDepartmentActive(session, d.departmentId, !d.active);
                if (!r) io.showError(r.error()); else out << "  " << d.name << (d.active ? " is switched off.\n" : " is in use again.\n");
            }
        } else {
            out << "  Please choose A, R, S or press Enter.\n";
        }
    }
}

void roomsScreen(ConsoleIO& io, AdminService& admin, const Session& session) {
    auto& out = io.out();
    while (true) {
        auto list = admin.listRooms(session);
        if (!list) { io.showError(list.error()); return; }
        const auto& rows = list.value();
        out << "\nCONSULTATION ROOMS\n  #   " << text::fitWidth("Room", 24) << text::fitWidth("Department", 24) << "Status\n";
        for (std::size_t i = 0; i < rows.size(); ++i) {
            out << "  " << text::fitWidth(std::to_string(i + 1) + ".", 4) << text::fitWidth(rows[i].name, 24)
                << text::fitWidth(rows[i].department.empty() ? "(none)" : rows[i].department, 24)
                << (rows[i].active ? "in use" : "switched off") << "\n";
        }
        out << "  A. Add a room   E. Edit one   S. Switch one off or on   Enter. Back\n";
        auto c = io.readLine("Choose: ");
        if (!c) return;
        const std::string choice = text::asciiLower(text::trimSpaces(*c));
        if (choice.empty()) return;
        if (choice != "a" && choice != "e" && choice != "s") { out << "  Please choose A, E, S or press Enter.\n"; continue; }

        const ConsultationRoom* room = nullptr;
        if (choice != "a") {
            if (rows.empty()) { out << "  There are no rooms yet.\n"; continue; }
            auto row = pickRow(io, "Room (row number): ", rows.size());
            if (!row) continue;
            room = &rows[*row];
        }
        if (choice == "s") {
            auto r = admin.setRoomActive(session, room->roomId, !room->active);
            if (!r) io.showError(r.error()); else out << "  " << room->name << (room->active ? " is switched off.\n" : " is in use again.\n");
            continue;
        }
        auto name = io.readLine(choice == "a" ? "Room name: " : "New name for " + room->name + ": ");
        if (!name) return;
        auto depts = admin.listDepartments(session);
        if (!depts) { io.showError(depts.error()); return; }
        out << "  Department (0 = none):\n";
        for (std::size_t i = 0; i < depts.value().size(); ++i) out << "    " << (i + 1) << ". " << depts.value()[i].name << "\n";
        auto dept = io.ask<std::int64_t>("Department number: ", [&](const std::string& s) {
            return input::parseWholeNumber(text::trimSpaces(s), "Department", 0, static_cast<std::int64_t>(depts.value().size()));
        });
        if (!dept) return;
        const std::int64_t deptId = *dept == 0 ? 0 : depts.value()[static_cast<std::size_t>(*dept - 1)].departmentId;
        auto r = choice == "a" ? admin.addRoom(session, *name, deptId) : admin.updateRoom(session, room->roomId, *name, deptId);
        if (!r) io.showError(r.error()); else out << "  Room saved.\n";
    }
}

void doctorMappingScreen(ConsoleIO& io, AdminService& admin, const Session& session) {
    auto& out = io.out();
    while (true) {
        auto docs = admin.listDoctorMappings(session);
        if (!docs) { io.showError(docs.error()); return; }
        auto depts = admin.listDepartments(session);
        if (!depts) { io.showError(depts.error()); return; }
        out << "\nDOCTOR-DEPARTMENT MAPPING\n";
        if (docs.value().empty()) { out << "  No doctor has a profile yet. Set hours with Admin option 2 first.\n"; return; }
        out << "  #   " << text::fitWidth("Doctor", 20) << "Department\n";
        for (std::size_t i = 0; i < docs.value().size(); ++i) {
            out << "  " << text::fitWidth(std::to_string(i + 1) + ".", 4) << text::fitWidth("Dr " + docs.value()[i].username, 20)
                << docs.value()[i].department << "\n";
        }
        auto row = pickRow(io, "Doctor to move (row number, Enter to go back): ", docs.value().size());
        if (!row) return;
        out << "  Departments:\n";
        for (std::size_t i = 0; i < depts.value().size(); ++i) {
            out << "    " << (i + 1) << ". " << depts.value()[i].name << (depts.value()[i].active ? "" : " (switched off)") << "\n";
        }
        if (depts.value().empty()) { out << "  There are no departments yet.\n"; return; }
        auto d = pickRow(io, "Department number (Enter to cancel): ", depts.value().size());
        if (!d) continue;
        auto r = admin.assignDoctorDepartment(session, docs.value()[*row].staffId, depts.value()[*d].departmentId);
        if (!r) io.showError(r.error());
        else out << "  Dr " << r.value().username << " is now in " << r.value().department << ".\n";
    }
}
}  // namespace

void masterDataScreen(ConsoleIO& io, AdminService& admin, const Session& session) {
    auto& out = io.out();
    while (true) {
        out << "\n--- MASTER DATA ---\n  1. Departments\n  2. Consultation rooms\n  3. Doctor-department mapping\n  0. Back\n";
        auto c = io.readLine("Choose an option: ");
        if (!c) return;
        const std::string choice = text::trimSpaces(*c);
        if (choice == "0" || choice.empty()) return;
        // The first call checks the role, so a denied user sees one message and goes back.
        if (choice == "1") departmentsScreen(io, admin, session);
        else if (choice == "2") roomsScreen(io, admin, session);
        else if (choice == "3") doctorMappingScreen(io, admin, session);
        else out << "  Please choose 0, 1, 2 or 3.\n";
    }
}

void settingsScreen(ConsoleIO& io, AdminService& admin, const Session& session) {
    auto& out = io.out();
    while (true) {
        auto list = admin.listSettings(session);
        if (!list) { io.showError(list.error()); return; }
        out << "\n--- SYSTEM SETTINGS ---   (a change applies from the next operation; no restart is needed)\n";
        int n = 1;
        for (const auto& s : list.value()) {
            out << "  " << n++ << ". " << s.info.label << "\n     Now: " << s.value << " " << s.info.unit
                << "   (allowed " << s.info.min << " to " << s.info.max << ", built-in default " << s.defaultValue
                << ")\n";
        }
        out << "  0. Back\n";
        auto c = io.readLine("Choose a setting to change: ");
        if (!c) return;
        const std::string choice = text::trimSpaces(*c);
        if (choice == "0" || choice.empty()) return;
        if (choice.size() != 1 || choice[0] < '1' || choice[0] > static_cast<char>('0' + list.value().size())) {
            out << "  Please choose a number from 0 to " << list.value().size() << ".\n";
            continue;
        }
        const SettingValue& chosen = list.value()[static_cast<std::size_t>(choice[0] - '1')];
        auto value = io.readLine("New value in " + std::string(chosen.info.unit) + " (Enter to cancel): ");
        if (!value || text::trimSpaces(*value).empty()) { out << "Cancelled. Nothing was changed.\n"; continue; }
        auto saved = admin.updateSetting(session, chosen.info.id, *value);
        if (!saved) { io.showError(saved.error()); continue; }
        out << "  Saved: " << saved.value().info.key << " is now " << saved.value().value << " "
            << saved.value().info.unit << ".\n";
    }
}

void reportsScreen(ConsoleIO& io, AdminService& admin, const Session& session) {
    auto& out = io.out();
    auto from = io.ask<Date>("From date (DD-MM-YYYY): ", [](const std::string& s) { return input::parseDate(text::trimSpaces(s), "From date"); });
    if (!from) return;
    auto to = io.ask<Date>("To date (DD-MM-YYYY): ", [](const std::string& s) { return input::parseDate(text::trimSpaces(s), "To date"); });
    if (!to) return;

    auto result = admin.report(session, *from, *to);
    if (!result) { io.showError(result.error()); return; }
    const HospitalReport& r = result.value();

    out << "\nHOSPITAL SUMMARY, " << displayDate(r.from) << " to " << displayDate(r.to) << "\n";
    out << "\nPatients\n"
        << "  Registered patients (all time):   " << r.registeredPatients << "\n"
        << "  Different patients seen or booked: " << r.patientsSeen << "\n";
    out << "\nAppointments\n"
        << "  Total " << r.appointments << "   Booked " << r.booked << "   Completed " << r.completed
        << "   Cancelled " << r.cancelled << "\n"
        << "  Consultations recorded: " << r.consultations << "\n";
    out << "\nBilling and payments\n"
        << "  Bills generated: " << r.billsCount << ", total " << money(r.billedPaise) << "\n"
        << "  Payments received (revenue): " << r.paymentsCount << ", total " << money(r.collectedPaise) << "\n";
    for (const auto& m : r.byMode) out << "    " << text::fitWidth(m.mode, 12) << money(m.paise) << "\n";
    out << "  Still unpaid on those bills: " << money(r.outstandingPaise) << "\n";

    out << "\nDay by day (days with appointments)\n";
    if (r.days.empty()) out << "  None.\n";
    else {
        out << "  " << text::fitWidth("Date", 12) << text::fitWidth("Patients", 10) << text::fitWidth("Booked", 8)
            << text::fitWidth("Done", 6) << "Cancel\n";
        for (const auto& d : r.days) {
            out << "  " << text::fitWidth(displayDate(d.date), 12) << text::fitWidth(std::to_string(d.patientsSeen), 10)
                << text::fitWidth(std::to_string(d.booked), 8) << text::fitWidth(std::to_string(d.completed), 6)
                << d.cancelled << "\n";
        }
    }
    out << "\nDoctor-wise appointments\n";
    if (r.doctors.empty()) out << "  None.\n";
    else {
        out << "  " << text::fitWidth("Doctor", 18) << text::fitWidth("Department", 20) << text::fitWidth("Total", 7)
            << text::fitWidth("Booked", 8) << text::fitWidth("Done", 6) << "Cancel\n";
        for (const auto& d : r.doctors) {
            out << "  " << text::fitWidth("Dr " + d.doctor, 18) << text::fitWidth(d.department, 20)
                << text::fitWidth(std::to_string(d.total()), 7) << text::fitWidth(std::to_string(d.booked), 8)
                << text::fitWidth(std::to_string(d.completed), 6) << d.cancelled << "\n";
        }
    }
    out << "\nStaff accounts\n";
    for (const auto& s : r.staff) {
        out << "  " << text::fitWidth(s.role, 16) << s.active << " active";
        if (s.deactivated > 0) out << ", " << s.deactivated << " deactivated";
        out << "\n";
    }
}

}  // namespace hms
