// Laboratory and pharmacy screens (issue #8).

#include "ui/lab_pharmacy_screens.hpp"

#include <ostream>

#include "billing/receipt.hpp"
#include "core/text.hpp"
#include "patient/uhid.hpp"
#include "ui/console_app.hpp"

namespace hms {

namespace {
const char* kCancelled = "Cancelled. Nothing was saved.\n";

// Asks until a printed number (LAB-000012, 12, ...) is typed; Enter or end of input goes back.
std::optional<std::int64_t> askNumber(ConsoleIO& io, const std::string& prompt, const std::string& what,
                                      std::optional<std::int64_t> (*parse)(std::string_view)) {
    while (true) {
        auto line = io.readLine(prompt);
        if (!line || text::trimSpaces(*line).empty()) return std::nullopt;
        if (auto id = parse(*line)) return id;
        io.out() << "  ! " << what << ": enter the number as printed, for example 12\n";
    }
}

std::optional<std::int64_t> askUhid(ConsoleIO& io) {
    while (true) {
        auto line = io.readLine("Patient UHID (Enter to go back): ");
        if (!line || text::trimSpaces(*line).empty()) return std::nullopt;
        if (auto id = parseUhid(*line)) return id;
        io.out() << "  ! UHID: must look like HMS-000123 or 123\n";
    }
}

void showOrderHeader(std::ostream& out) {
    out << "  " << text::fitWidth("Order", 12) << text::fitWidth("UHID", 12) << text::fitWidth("Patient", 20)
        << text::fitWidth("Test", 24) << text::fitWidth("Ordered", 18) << "Result\n";
}

void showOrderRow(std::ostream& out, const LabOrderView& o, int offset) {
    out << "  " << text::fitWidth(formatLabOrderId(o.orderId), 12) << text::fitWidth(formatUhid(o.uhid), 12)
        << text::fitWidth(o.patientName, 20) << text::fitWidth(o.testName, 24)
        << text::fitWidth(localDateTimeText(o.orderedAt, offset), 18)
        << (o.hasResult() ? "saved (revision " + std::to_string(o.latest->revision) + ")" : std::string("waiting")) << "\n";
}

void showOrderDetail(std::ostream& out, const LabOrderView& o, int offset) {
    out << "  Order " << formatLabOrderId(o.orderId) << ": " << o.testName << "\n"
        << "  Patient: " << o.patientName << " (" << formatUhid(o.uhid) << "), ordered by Dr " << o.doctorUsername
        << " on " << localDateTimeText(o.orderedAt, offset) << "\n";
    if (o.instructions) out << "  Doctor's instructions: " << *o.instructions << "\n";
    if (o.hasResult()) {
        out << "  Result (revision " << o.latest->revision << ", entered by " << o.latest->enteredBy << " on "
            << localDateTimeText(o.latest->enteredAt, offset) << "):\n    " << o.latest->text << "\n";
    } else {
        out << "  No result entered yet.\n";
    }
}

void showStock(std::ostream& out, const std::vector<Medicine>& rows, std::int64_t threshold) {
    out << "  " << text::fitWidth("Number", 12) << text::fitWidth("Medicine", 30) << text::fitWidth("In stock", 12)
        << "Alert\n";
    bool anyLow = false;
    for (const auto& m : rows) {
        out << "  " << text::fitWidth(formatMedicineId(m.medicineId), 12) << text::fitWidth(m.name, 30)
            << text::fitWidth(std::to_string(m.stockQuantity) + " " + m.unit, 12)
            << (!m.active ? "UNAVAILABLE" : m.lowStock ? "LOW STOCK" : "") << "\n";
        anyLow = anyLow || m.lowStock;
    }
    if (anyLow) out << "  LOW STOCK means fewer than " << threshold << " left.\n";
}

void showLines(std::ostream& out, const std::vector<DispenseLine>& rows) {
    out << "  " << text::fitWidth("Line", 12) << text::fitWidth("UHID", 12) << text::fitWidth("Patient", 20)
        << text::fitWidth("Medicine (as written)", 26) << text::fitWidth("Dosage", 16) << "Dispensed\n";
    for (const auto& l : rows) {
        out << "  " << text::fitWidth(formatPrescriptionId(l.itemId), 12) << text::fitWidth(formatUhid(l.uhid), 12)
            << text::fitWidth(l.patientName, 20) << text::fitWidth(l.medicineName, 26) << text::fitWidth(l.dosage, 16)
            << l.dispensed << " of " << l.prescribed << "\n";
    }
}
}  // namespace

// ------------------------------------------------------------ Lab Technician

void labMenu(ConsoleIO& io, LabService& lab, const Session& session) {
    auto& out = io.out();
    const int offset = lab.policy().utcOffsetMinutes;
    bool blocked = false;  // after an "outcome unknown" result no more changes are made (SRS 2.5)
    while (!io.exhausted()) {
        out << "\n------------------------------------------------------------------------------\n"
               "LAB MENU  -  " << session.displayName
            << "\n------------------------------------------------------------------------------\n"
               "  1. Tests waiting for a result\n"
               "  2. Enter or update a test result\n"
               "  3. Tests of one patient\n"
               "  0. Log out\n";
        auto c = io.readLine("Choose an option: ");
        if (!c || text::trimSpaces(*c) == "0") return;
        const std::string choice = text::trimSpaces(*c);

        if (choice == "1") {
            auto rows = lab.listPendingOrders(session);
            if (!rows) { io.showError(rows.error()); continue; }
            out << "\nTESTS WAITING FOR A RESULT (oldest first)\n";
            if (rows.value().empty()) { out << "  None. Every ordered test has a result.\n"; continue; }
            showOrderHeader(out);
            for (const auto& o : rows.value()) showOrderRow(out, o, offset);
        } else if (choice == "2") {
            if (blocked) { out << "  ! Changes are blocked until the application is restarted.\n"; continue; }
            out << "\nENTER OR UPDATE A TEST RESULT\n";
            auto id = askNumber(io, "Order number (for example LAB-000012; Enter to go back): ", "Order number", parseLabOrderId);
            if (!id) continue;
            auto order = lab.getOrder(session, *id);
            if (!order) { io.showError(order.error()); continue; }
            showOrderDetail(out, order.value(), offset);
            auto text = io.readLine(order.value().hasResult() ? "New result text (Enter to cancel): " : "Result (Enter to cancel): ");
            if (!text || text::trimSpaces(*text).empty()) { out << kCancelled; continue; }
            auto saved = lab.recordResult(session, *id, *text);
            if (!saved) {
                io.showError(saved.error());
                if (saved.error().kind == ErrorKind::OutcomeUnknown) blocked = true;
                continue;
            }
            out << "  Saved as revision " << saved.value().latest->revision << " of " << formatLabOrderId(*id)
                << ". The treating doctor can now see it.\n";
        } else if (choice == "3") {
            auto uhid = askUhid(io);
            if (!uhid) continue;
            auto rows = lab.listOrdersForPatient(session, *uhid);
            if (!rows) { io.showError(rows.error()); continue; }
            out << "\nTESTS OF " << formatUhid(*uhid) << " (newest first)\n";
            if (rows.value().empty()) { out << "  No tests ordered.\n"; continue; }
            for (const auto& o : rows.value()) showOrderDetail(out, o, offset);
        } else {
            out << "  Please choose 0, 1, 2 or 3.\n";
        }
    }
}

// ------------------------------------------------------------ Pharmacist

void pharmacyMenu(ConsoleIO& io, PharmacyService& pharmacy, const Session& session) {
    auto& out = io.out();
    const std::int64_t threshold = pharmacy.policy().labPharmacy.lowStockThreshold;
    bool blocked = false;
    while (!io.exhausted()) {
        out << "\n------------------------------------------------------------------------------\n"
               "PHARMACY MENU  -  " << session.displayName
            << "\n------------------------------------------------------------------------------\n"
               "  1. Check the medicine stock\n"
               "  2. Add a medicine\n"
               "  3. Restock a medicine\n"
               "  4. Prescriptions waiting to be dispensed\n"
               "  5. Dispense a prescription line\n"
               "  6. Prescriptions of one patient\n"
               "  7. Correct a medicine's name or unit\n"
               "  8. Correct a stock count after counting the shelf\n"
               "  9. Make a medicine unavailable or available again\n"
               "  0. Log out\n";
        auto c = io.readLine("Choose an option: ");
        if (!c || text::trimSpaces(*c) == "0") return;
        const std::string choice = text::trimSpaces(*c);

        auto showStockList = [&]() -> bool {
            auto rows = pharmacy.listStock(session);
            if (!rows) { io.showError(rows.error()); return false; }
            if (rows.value().empty()) { out << "  No medicine is in the stock list yet. Use option 2 to add one.\n"; return false; }
            showStock(out, rows.value(), threshold);
            return true;
        };
        auto fail = [&](const Error& e) {
            io.showError(e);
            if (e.kind == ErrorKind::OutcomeUnknown) blocked = true;
        };

        if (choice == "1") {
            out << "\nMEDICINE STOCK\n";
            showStockList();
        } else if (choice == "2") {
            if (blocked) { out << "  ! Changes are blocked until the application is restarted.\n"; continue; }
            out << "\nADD A MEDICINE   (press Enter at the name to cancel)\n";
            MedicineDraft d;
            auto name = io.readLine("Medicine name: ");
            if (!name || text::trimSpaces(*name).empty()) { out << kCancelled; continue; }
            d.name = *name;
            auto unit = io.readLine("Unit (for example tablet, ml, strip): ");
            if (!unit) { out << kCancelled; continue; }
            d.unit = *unit;
            auto stock = io.readLine("Opening stock (Enter for 0): ");
            if (!stock) { out << kCancelled; continue; }
            d.initialStock = *stock;
            auto added = pharmacy.addMedicine(session, d);
            if (!added) { fail(added.error()); continue; }
            out << "  Added " << added.value().name << " as " << formatMedicineId(added.value().medicineId) << " with "
                << added.value().stockQuantity << " " << added.value().unit << " in stock.\n";
            if (added.value().lowStock) out << "  LOW STOCK: fewer than " << threshold << " left.\n";
        } else if (choice == "3") {
            if (blocked) { out << "  ! Changes are blocked until the application is restarted.\n"; continue; }
            out << "\nRESTOCK A MEDICINE\n";
            if (!showStockList()) continue;
            auto id = askNumber(io, "Medicine number (for example MED-000003; Enter to go back): ", "Medicine number", parseMedicineId);
            if (!id) continue;
            auto qty = io.readLine("Quantity received: ");
            if (!qty) { out << kCancelled; continue; }
            auto after = pharmacy.restock(session, *id, *qty);
            if (!after) { fail(after.error()); continue; }
            out << "  " << after.value().name << " now has " << after.value().stockQuantity << " " << after.value().unit
                << " in stock.\n";
            if (after.value().lowStock) out << "  LOW STOCK: fewer than " << threshold << " left.\n";
        } else if (choice == "4") {
            auto rows = pharmacy.listPendingPrescriptions(session);
            if (!rows) { io.showError(rows.error()); continue; }
            out << "\nPRESCRIPTIONS WAITING TO BE DISPENSED (oldest first)\n";
            if (rows.value().empty()) { out << "  None. Every prescription line has been dispensed.\n"; continue; }
            showLines(out, rows.value());
        } else if (choice == "5") {
            if (blocked) { out << "  ! Changes are blocked until the application is restarted.\n"; continue; }
            out << "\nDISPENSE A PRESCRIPTION LINE\n";
            auto pending = pharmacy.listPendingPrescriptions(session);
            if (!pending) { io.showError(pending.error()); continue; }
            if (pending.value().empty()) { out << "  Nothing is waiting to be dispensed.\n"; continue; }
            showLines(out, pending.value());
            auto lineId = askNumber(io, "Line number (for example RX-000045; Enter to go back): ", "Line number", parsePrescriptionId);
            if (!lineId) continue;
            const DispenseLine* chosen = nullptr;
            for (const auto& l : pending.value()) if (l.itemId == *lineId) chosen = &l;
            if (!chosen) { out << "  ! That line is not waiting to be dispensed.\n"; continue; }
            out << "  " << chosen->medicineName << ", " << chosen->dosage << " - " << chosen->remaining()
                << " still to dispense for " << chosen->patientName << " (" << formatUhid(chosen->uhid) << ").\n";
            if (!showStockList()) continue;
            auto medId = askNumber(io, "Medicine number to give (Enter to go back): ", "Medicine number", parseMedicineId);
            if (!medId) continue;
            auto qty = io.readLine("Quantity to dispense (Enter for " + std::to_string(chosen->remaining()) + "): ");
            if (!qty) { out << kCancelled; continue; }
            const std::string qtyText = text::trimSpaces(*qty).empty() ? std::to_string(chosen->remaining()) : *qty;
            auto sure = io.askYesNo("Dispense " + qtyText + " now? (Y/N): ");
            if (!sure || !*sure) { out << kCancelled; continue; }
            auto done = pharmacy.dispense(session, *lineId, *medId, qtyText);
            if (!done) { fail(done.error()); continue; }
            const auto& o = done.value();
            out << "  Dispensed " << o.quantity << " " << o.unit << " of " << o.medicineName << ". "
                << (o.remainingToDispense > 0 ? std::to_string(o.remainingToDispense) + " still to dispense on this line. "
                                              : std::string("The line is complete. "))
                << o.stockAfter << " " << o.unit << " left in stock.\n";
            if (o.lowStock) out << "  ! LOW STOCK: " << o.medicineName << " is below " << o.threshold << ". Please reorder.\n";
        } else if (choice == "6") {
            auto uhid = askUhid(io);
            if (!uhid) continue;
            auto rows = pharmacy.listPrescriptionsForPatient(session, *uhid);
            if (!rows) { io.showError(rows.error()); continue; }
            out << "\nPRESCRIPTIONS OF " << formatUhid(*uhid) << " (newest first)\n";
            if (rows.value().empty()) { out << "  No prescriptions.\n"; continue; }
            showLines(out, rows.value());
        } else if (choice == "7") {
            if (blocked) { out << "  ! Changes are blocked until the application is restarted.\n"; continue; }
            out << "\nCORRECT A MEDICINE'S NAME OR UNIT\n";
            if (!showStockList()) continue;
            auto id = askNumber(io, "Medicine number (Enter to go back): ", "Medicine number", parseMedicineId);
            if (!id) continue;
            MedicineEdit edit;
            auto name = io.readLine("Correct name: ");
            if (!name) { out << kCancelled; continue; }
            edit.name = *name;
            auto unit = io.readLine("Correct unit: ");
            if (!unit) { out << kCancelled; continue; }
            edit.unit = *unit;
            auto saved = pharmacy.updateMedicine(session, *id, edit);
            if (!saved) { fail(saved.error()); continue; }
            out << "  " << formatMedicineId(saved.value().medicineId) << " is now " << saved.value().name << " ("
                << saved.value().unit << ").\n";
        } else if (choice == "8") {
            if (blocked) { out << "  ! Changes are blocked until the application is restarted.\n"; continue; }
            out << "\nCORRECT A STOCK COUNT\n";
            if (!showStockList()) continue;
            auto id = askNumber(io, "Medicine number (Enter to go back): ", "Medicine number", parseMedicineId);
            if (!id) continue;
            auto counted = io.readLine("Quantity actually counted: ");
            if (!counted) { out << kCancelled; continue; }
            auto why = io.readLine("Reason (for example 'broken strips'): ");
            if (!why) { out << kCancelled; continue; }
            auto saved = pharmacy.correctStock(session, *id, *counted, *why);
            if (!saved) { fail(saved.error()); continue; }
            out << "  " << saved.value().name << " now has " << saved.value().stockQuantity << " " << saved.value().unit
                << " in stock.\n";
            if (saved.value().lowStock) out << "  LOW STOCK: fewer than " << threshold << " left.\n";
        } else if (choice == "9") {
            if (blocked) { out << "  ! Changes are blocked until the application is restarted.\n"; continue; }
            out << "\nMAKE A MEDICINE UNAVAILABLE OR AVAILABLE AGAIN\n";
            auto rows = pharmacy.listStock(session);
            if (!rows) { io.showError(rows.error()); continue; }
            if (rows.value().empty()) { out << "  No medicine is in the stock list yet.\n"; continue; }
            showStock(out, rows.value(), threshold);
            auto id = askNumber(io, "Medicine number (Enter to go back): ", "Medicine number", parseMedicineId);
            if (!id) continue;
            bool makeAvailable = false;
            for (const auto& m : rows.value()) if (m.medicineId == *id) makeAvailable = !m.active;
            auto saved = pharmacy.setMedicineActive(session, *id, makeAvailable);
            if (!saved) { fail(saved.error()); continue; }
            out << "  " << saved.value().name << " is now " << (saved.value().active ? "available" : "unavailable")
                << ".\n";
        } else {
            out << "  Please choose a number from 0 to 9.\n";
        }
    }
}

// ------------------------------------------------------------ Doctor: lab results

void ConsoleApp::labResultsScreen() {
    auto& out = io_.out();
    std::int64_t uhid = 0;
    while (true) {
        auto line = io_.readLine("Patient UHID (Enter to go back): ");
        if (!line || text::trimSpaces(*line).empty()) return;
        auto id = parseUhid(*line);
        if (id) { uhid = *id; break; }
        out << "  ! UHID: must look like HMS-000123 or 123\n";
    }
    auto r = lab_->listResultsForPatient(session_, uhid);
    if (!r) { handleFailure(r.error()); return; }
    out << "\nLAB RESULTS of " << formatUhid(uhid) << " for the tests you ordered (newest first)\n";
    if (r.value().empty()) { out << "  You have not ordered any test for this patient.\n"; return; }
    const int offset = lab_->policy().utcOffsetMinutes;
    for (const auto& o : r.value()) showOrderDetail(out, o, offset);
}

}  // namespace hms
