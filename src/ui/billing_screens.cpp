// Billing screens (issue #7): the Admin's consultation fee and the Receptionist's
// bill, payment, receipt and balance screens. Like the rest of ConsoleApp, they
// hold no business rule: they read input, call one service function and show
// the result.

#include <ostream>

#include "appointment/appointment.hpp"
#include "billing/receipt.hpp"
#include "core/money.hpp"
#include "core/text.hpp"
#include "patient/uhid.hpp"
#include "ui/billing_screens.hpp"
#include "ui/console_app.hpp"
#include "validation/input.hpp"

namespace hms {

namespace {
const char* kCancelled = "Cancelled. Nothing was saved.\n";

void showSummaries(std::ostream& out, const std::vector<BillSummary>& rows, int offset) {
    out << "  " << text::fitWidth("Bill", 13) << text::fitWidth("UHID", 12) << text::fitWidth("Patient", 22)
        << text::fitWidth("Date", 18) << text::fitWidth("Total", 14) << text::fitWidth("Paid", 14) << "Balance\n";
    for (const auto& b : rows) {
        out << "  " << text::fitWidth(formatBillId(b.billId), 13) << text::fitWidth(formatUhid(b.uhid), 12)
            << text::fitWidth(b.patientName, 22) << text::fitWidth(localDateTimeText(b.createdAt, offset), 18)
            << text::fitWidth(formatAmount(b.totalPaise), 14) << text::fitWidth(formatAmount(b.paidPaise), 14)
            << formatAmount(b.balancePaise()) << "\n";
    }
}

std::optional<std::int64_t> askId(ConsoleIO& io, const std::string& prompt, const std::string& what,
                                  std::optional<std::int64_t> (*parse)(std::string_view)) {
    while (true) {
        auto line = io.readLine(prompt);
        if (!line || text::trimSpaces(*line).empty()) return std::nullopt;
        if (auto id = parse(*line)) return id;
        io.out() << "  ! " << what << ": enter the number as printed, for example 12\n";
    }
}

// Offers to save a receipt as a text file the cashier can print.
void offerSave(ConsoleIO& io, const std::string& defaultName, const std::string& content) {
    auto& out = io.out();
    auto save = io.askYesNo("Save a printable copy to a file? (Y/N): ");
    if (!save || !*save) return;
    auto name = io.readLine("File name (Enter for " + defaultName + "): ");
    if (!name) return;
    std::string path = text::trimSpaces(*name);
    if (path.empty()) path = defaultName;
    const std::string error = writeTextFile(path, content);
    if (error.empty()) out << "  Saved to " << path << ".\n";
    else out << "  ! " << error << "\n";
}
}  // namespace

// ------------------------------------------------------------ Admin: consultation fee

void consultationFeeScreen(ConsoleIO& io, BillingService& billing, const Session& session) {
    auto& out = io.out();
    auto doctors = billing.listDoctorFees(session);
    if (!doctors) { io.showError(doctors.error()); return; }
    out << "\nCONSULTATION FEES\n";
    if (doctors.value().empty()) {
        out << "  No doctor has a profile yet. Use option 2 first to set a doctor's department and hours.\n";
        return;
    }
    out << "  " << text::fitWidth("No.", 5) << text::fitWidth("Doctor", 20) << text::fitWidth("Department", 22) << "Fee\n";
    for (std::size_t i = 0; i < doctors.value().size(); ++i) {
        const auto& d = doctors.value()[i];
        out << "  " << text::fitWidth(std::to_string(i + 1), 5) << text::fitWidth(d.username, 20)
            << text::fitWidth(d.department, 22) << (d.feePaise ? formatRupees(*d.feePaise) : std::string("not set")) << "\n";
    }
    const std::size_t count = doctors.value().size();
    auto pick = io.ask<std::size_t>("Doctor number to change (Enter to go back): ",
                                    [&](const std::string& s) -> Result<std::size_t> {
        const std::string t = text::trimSpaces(s);
        if (t.empty()) return std::size_t{0};
        auto n = input::parseWholeNumber(t, "Doctor number", 1, static_cast<std::int64_t>(count));
        if (!n) return n.error();
        return static_cast<std::size_t>(n.value());
    });
    if (!pick || *pick == 0) return;
    const auto& doctor = doctors.value()[*pick - 1];
    auto fee = io.readLine("New consultation fee in rupees for Dr " + doctor.username + " (for example 500 or 500.50): ");
    if (!fee) { out << kCancelled; return; }
    auto saved = billing.setConsultationFee(session, doctor.staffId, *fee);
    if (!saved) { io.showError(saved.error()); return; }
    out << "  Fee for Dr " << saved.value().username << " is now " << formatRupees(*saved.value().feePaise) << ".\n"
        << "  Bills already made keep the fee they were made with.\n";
}

// ------------------------------------------------------------ Receptionist: bill a visit (HMS-F-030)

void ConsoleApp::billVisitScreen() {
    auto& out = io_.out();
    if (blockedForChanges()) return;
    const BillingPolicy& bp = billing_->policy().billing;
    out << "\nBILL A COMPLETED VISIT\n"
        << "To go back, press Enter at the first prompt, or Ctrl+D (Linux) / Ctrl+Z then Enter (Windows).\n";
    auto appointmentId = askId(io_, "Appointment number, e.g. APT-000012: ", "Appointment", parseAppointmentId);
    if (!appointmentId) return;

    auto preview = billing_->previewBill(session_, *appointmentId);
    if (!preview) { handleFailure(preview.error()); return; }
    const auto& p = preview.value();
    out << "  " << formatAppointmentId(p.appointmentId) << "  " << p.patientName << " (" << formatUhid(p.uhid) << ")  Dr "
        << p.doctorName << "\n  Consultation fee: " << formatRupees(p.consultationFeePaise) << "\n";

    std::vector<std::string> testPrices;
    for (const auto& test : p.tests) {
        auto price = io_.ask<std::string>("  * Price in rupees for test \"" + test + "\" (0 if no charge): ",
                                          [&](const std::string& s) -> Result<std::string> {
            auto v = input::requireLine(s, "Price");
            if (!v) return v.error();
            auto n = input::parseMoneyPaise(v.value(), "Price", false, bp.maxUnitPricePaise);
            if (!n) return n.error();
            return v.value();
        });
        if (!price) { out << kCancelled; return; }
        testPrices.push_back(*price);
    }

    std::vector<ExtraChargeDraft> extras;
    while (extras.size() < bp.maxExtraLines) {
        auto more = io_.askYesNo(extras.empty() ? "Add another service charge (for example a dressing)? (Y/N): "
                                                : "Add one more service charge? (Y/N): ");
        if (!more) { out << kCancelled; return; }
        if (!*more) break;
        ExtraChargeDraft x;
        auto d = io_.ask<std::string>("  * Description: ", [&](const std::string& s) -> Result<std::string> {
            auto v = input::requireLine(s, "Description");
            if (!v) return v.error();
            if (text::utf8Length(v.value()) > bp.maxDescriptionChars) {
                return Error::validation("Description", "must be at most " + std::to_string(bp.maxDescriptionChars) + " characters");
            }
            return v.value();
        });
        if (!d) { out << kCancelled; return; }
        auto q = io_.ask<std::string>("    Quantity (Enter for 1): ", [&](const std::string& s) -> Result<std::string> {
            const std::string t = text::trimSpaces(s);
            if (t.empty()) return t;
            auto n = input::parseWholeNumber(t, "Quantity", 1, bp.maxLineQuantity);
            if (!n) return n.error();
            return t;
        });
        if (!q) { out << kCancelled; return; }
        auto price = io_.ask<std::string>("  * Price of one in rupees: ", [&](const std::string& s) -> Result<std::string> {
            auto v = input::requireLine(s, "Price");
            if (!v) return v.error();
            auto n = input::parseMoneyPaise(v.value(), "Price", true, bp.maxUnitPricePaise);
            if (!n) return n.error();
            return v.value();
        });
        if (!price) { out << kCancelled; return; }
        x.description = *d;
        x.quantity = *q;
        x.unitPrice = *price;
        extras.push_back(x);
    }

    auto confirm = io_.askYesNo("Create the bill? It cannot be changed afterwards. (Y/N): ");
    if (!confirm || !*confirm) { out << kCancelled; return; }
    auto bill = billing_->generateBill(session_, *appointmentId, testPrices, extras);
    if (!bill) { handleFailure(bill.error()); return; }
    out << "\n  Bill " << formatBillId(bill.value().billId) << " created.\n"
        << renderBillText(bill.value(), billing_->policy().utcOffsetMinutes);
}

// ------------------------------------------------------------ Receptionist: payment and receipt (HMS-F-031, F-032)

void ConsoleApp::paymentScreen() {
    auto& out = io_.out();
    if (blockedForChanges()) return;
    out << "\nRECORD A PAYMENT\n"
        << "To go back, press Enter at the first prompt, or Ctrl+D (Linux) / Ctrl+Z then Enter (Windows).\n";
    auto billId = askId(io_, "Bill number, e.g. BILL-000012: ", "Bill", parseBillId);
    if (!billId) return;
    auto bill = billing_->getBill(session_, *billId);
    if (!bill) { handleFailure(bill.error()); return; }
    out << renderBillText(bill.value(), billing_->policy().utcOffsetMinutes);
    if (bill.value().balancePaise() <= 0) { out << "  This bill is already paid in full.\n"; return; }

    auto amount = io_.readLine("* Amount received in rupees (outstanding " + formatRupees(bill.value().balancePaise()) + "): ");
    if (!amount) { out << kCancelled; return; }
    auto mode = io_.ask<PaymentMode>("* Payment mode: 1. Cash  2. Card  3. Insurance: ",
                                     [&](const std::string& s) -> Result<PaymentMode> {
        const std::string t = text::trimSpaces(s);
        if (t == "1") return PaymentMode::Cash;
        if (t == "2") return PaymentMode::Card;
        if (t == "3") return PaymentMode::Insurance;
        return Error::validation("Payment mode", "must be 1, 2 or 3");
    });
    if (!mode) { out << kCancelled; return; }
    auto reference = io_.readLine("  Reference (receipt or claim number, optional; never a card number): ");
    if (!reference) { out << kCancelled; return; }

    auto outcome = billing_->recordPayment(session_, *billId, *amount, *mode, *reference);
    if (!outcome) { handleFailure(outcome.error()); return; }
    out << "  Payment of " << formatRupees(outcome.value().payment.amountPaise) << " recorded as "
        << formatReceiptId(outcome.value().payment.paymentId) << ". Balance: "
        << formatRupees(outcome.value().balanceAfterPaise) << " (" << to_string(outcome.value().status) << ").\n";
    printReceipt(outcome.value().payment.paymentId);
}

void ConsoleApp::printReceipt(std::int64_t paymentId) {
    auto& out = io_.out();
    auto receipt = billing_->generateReceipt(session_, paymentId);
    if (!receipt) { handleFailure(receipt.error()); return; }
    const std::string body = renderReceiptText(receipt.value());
    out << "\n" << body;
    offerSave(io_, receipt.value().receiptNumber + ".txt", body);
}

void ConsoleApp::reprintReceiptScreen() {
    auto& out = io_.out();
    out << "\nPRINT A RECEIPT AGAIN\n";
    auto id = askId(io_, "Receipt number, e.g. RCPT-000012 (Enter to go back): ", "Receipt", parseReceiptId);
    if (!id) return;
    printReceipt(*id);
}

// ------------------------------------------------------------ Receptionist: bills and balances

void ConsoleApp::billsScreen() {
    auto& out = io_.out();
    const int offset = billing_->policy().utcOffsetMinutes;
    out << "\nBILLS AND BALANCES\n";
    auto line = io_.readLine("Patient UHID (Enter to list every bill with a balance): ");
    if (!line) return;
    const std::string t = text::trimSpaces(*line);
    if (t.empty()) {
        auto rows = billing_->listOutstandingBills(session_);
        if (!rows) { handleFailure(rows.error()); return; }
        out << "  Bills with a balance to pay (oldest first)\n";
        if (rows.value().empty()) { out << "  None. Every bill is paid.\n"; return; }
        showSummaries(out, rows.value(), offset);
    } else {
        auto uhid = parseUhid(t);
        if (!uhid) { out << "  ! UHID: must look like HMS-000123 or 123\n"; return; }
        auto rows = billing_->listBillsForPatient(session_, *uhid);
        if (!rows) { handleFailure(rows.error()); return; }
        out << "  Bills of " << formatUhid(*uhid) << " (newest first)\n";
        if (rows.value().empty()) { out << "  No bills.\n"; return; }
        showSummaries(out, rows.value(), offset);
    }
    auto id = askId(io_, "Bill number to open (Enter to go back): ", "Bill", parseBillId);
    if (!id) return;
    auto bill = billing_->getBill(session_, *id);
    if (!bill) { handleFailure(bill.error()); return; }
    out << renderBillText(bill.value(), offset);
}

}  // namespace hms
