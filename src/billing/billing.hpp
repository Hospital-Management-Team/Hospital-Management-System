#pragma once
// Billing types (SRS HMS-F-030..032; SAD "Billing & Payment Module"; issue #7).
// All amounts are whole paise.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace hms {

enum class PaymentMode { Cash, Card, Insurance };
std::string to_string(PaymentMode m);                       // stored form: CASH / CARD / INSURANCE
std::string paymentModeLabel(PaymentMode m);                // "Cash" / "Card" / "Insurance"
std::optional<PaymentMode> paymentModeFromStorage(const std::string& s);

enum class BillStatus { Unpaid, PartiallyPaid, Paid };
std::string to_string(BillStatus s);                        // "Unpaid" / "Partly paid" / "Paid"

// Bill and receipt numbers: BILL-000001, RCPT-000001.
std::string formatBillId(std::int64_t id);
std::string formatReceiptId(std::int64_t id);
std::optional<std::int64_t> parseBillId(std::string_view text);     // "BILL-000012", "bill-12", "12"
std::optional<std::int64_t> parseReceiptId(std::string_view text);  // "RCPT-000012", "12"

// An "other applicable service charge" typed by reception, before validation.
struct ExtraChargeDraft {
    std::string description;
    std::string quantity;    // whole number, blank = 1
    std::string unitPrice;   // rupees, e.g. "150" or "150.50"
};

struct BillLine {
    std::int64_t lineId = 0;
    std::string kind;        // CONSULTATION / LAB_TEST / SERVICE
    std::string description;
    std::int64_t quantity = 1;
    std::int64_t unitPricePaise = 0;
    std::int64_t amountPaise = 0;
};

struct PaymentRecord {
    std::int64_t paymentId = 0;
    std::int64_t billId = 0;
    std::int64_t amountPaise = 0;
    PaymentMode mode = PaymentMode::Cash;
    std::optional<std::string> reference;
    std::string receivedBy;
    std::string paidAt;      // UTC, as stored
};

struct Bill {
    std::int64_t billId = 0;
    std::int64_t uhid = 0;
    std::string patientName;
    std::int64_t appointmentId = 0;
    std::string slotStart;   // hospital-local "YYYY-MM-DDTHH:MM"
    std::string doctorName;  // the doctor's username
    std::int64_t totalPaise = 0;
    std::string createdBy;
    std::string createdAt;   // UTC, as stored
    std::vector<BillLine> lines;
    std::vector<PaymentRecord> payments;
    std::int64_t paidPaise = 0;
    std::int64_t balancePaise() const { return totalPaise - paidPaise; }
    BillStatus status() const {
        if (paidPaise >= totalPaise) return BillStatus::Paid;
        return paidPaise > 0 ? BillStatus::PartiallyPaid : BillStatus::Unpaid;
    }
};

struct BillSummary {
    std::int64_t billId = 0;
    std::int64_t uhid = 0;
    std::string patientName;
    std::int64_t totalPaise = 0;
    std::int64_t paidPaise = 0;
    std::string createdAt;   // UTC, as stored
    std::int64_t balancePaise() const { return totalPaise - paidPaise; }
};

struct PaymentOutcome {
    PaymentRecord payment;
    std::int64_t balanceAfterPaise = 0;
    BillStatus status = BillStatus::Unpaid;
};

// Everything a printed receipt shows (AC-HMS-F-032: bill ID, itemised charges,
// payment mode and timestamp).
struct Receipt {
    std::string receiptNumber;   // RCPT-000001
    PaymentRecord payment;
    std::int64_t uhid = 0;
    std::string patientName;
    std::int64_t appointmentId = 0;
    std::string slotStart;
    std::string doctorName;
    std::vector<BillLine> lines;
    std::int64_t billTotalPaise = 0;
    std::int64_t paidToDatePaise = 0;   // up to and including this payment
    std::int64_t balanceAfterPaise = 0;
    std::string paidAtLocal;            // "DD-MM-YYYY HH:MM", hospital-local
};

// A doctor and the consultation fee the Admin has set (none yet = not billable).
struct DoctorFee {
    std::int64_t staffId = 0;
    std::string username;
    std::string department;
    std::optional<std::int64_t> feePaise;
};

// What reception sees before billing a visit: the fee and the tests the doctor
// ordered, which reception prices (the SRS bills "tests"; D-44).
struct BillPreview {
    std::int64_t appointmentId = 0;
    std::int64_t uhid = 0;
    std::string patientName;
    std::string doctorName;
    std::int64_t consultationFeePaise = 0;
    std::vector<std::string> tests;     // names, in the order prices are expected
};

}  // namespace hms
