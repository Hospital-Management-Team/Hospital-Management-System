#include "billing/receipt.hpp"

#include <fstream>

#include "core/clock.hpp"
#include "appointment/appointment.hpp"
#include "core/money.hpp"
#include "core/text.hpp"
#include "patient/uhid.hpp"

namespace hms {

namespace {
constexpr std::size_t kWidth = 78;

std::string rule(char c) { return std::string(kWidth, c); }

std::string centred(const std::string& s) {
    const std::size_t len = text::utf8Length(s);
    return std::string(len >= kWidth ? 0 : (kWidth - len) / 2, ' ') + s;
}

std::string right(const std::string& s, std::size_t width) {
    const std::size_t len = text::utf8Length(s);
    return std::string(len >= width ? 0 : width - len, ' ') + s;
}

// "label ........ value" over the full width.
std::string labelled(const std::string& label, const std::string& value) {
    const std::size_t used = text::utf8Length(label) + text::utf8Length(value);
    return label + std::string(used >= kWidth ? 1 : kWidth - used, ' ') + value;
}

std::string visitText(const std::string& slotStart) {
    // "YYYY-MM-DDTHH:MM" -> "DD-MM-YYYY HH:MM"
    if (slotStart.size() != 16) return slotStart;
    return slotStart.substr(8, 2) + "-" + slotStart.substr(5, 2) + "-" + slotStart.substr(0, 4) + " " +
           slotStart.substr(11, 5);
}

void appendLines(std::string& out, const std::vector<BillLine>& lines) {
    out += text::fitWidth("Item", 44) + right("Qty", 5) + right("Rate", 13) + right("Amount", 16) + "\n";
    out += rule('-') + "\n";
    for (const auto& l : lines) {
        out += text::fitWidth(l.description, 44) + right(std::to_string(l.quantity), 5) +
               right(formatAmount(l.unitPricePaise), 13) + right(formatAmount(l.amountPaise), 16) + "\n";
    }
    out += rule('-') + "\n";
}
}  // namespace

std::string localDateTimeText(const std::string& isoUtc, int utcOffsetMinutes) {
    // Stored form: "YYYY-MM-DDTHH:MM:SSZ".
    if (isoUtc.size() != 20 || isoUtc[10] != 'T' || isoUtc[19] != 'Z') return isoUtc;
    auto num = [&](std::size_t at, std::size_t n) {
        int v = 0;
        for (std::size_t i = at; i < at + n; ++i) {
            if (isoUtc[i] < '0' || isoUtc[i] > '9') return -1;
            v = v * 10 + (isoUtc[i] - '0');
        }
        return v;
    };
    const int y = num(0, 4), mo = num(5, 2), d = num(8, 2), h = num(11, 2), mi = num(14, 2), sec = num(17, 2);
    if (y < 0 || mo < 1 || mo > 12 || d < 1 || d > 31 || h < 0 || h > 23 || mi < 0 || mi > 59 || sec < 0 || sec > 59) {
        return isoUtc;
    }
    std::int64_t secs = daysFromCivil(y, mo, d) * 86400 + h * 3600 + mi * 60 + sec + std::int64_t{utcOffsetMinutes} * 60;
    std::int64_t days = secs / 86400;
    std::int64_t rest = secs % 86400;
    if (rest < 0) { rest += 86400; --days; }
    const Date date = civilFromDays(days);
    return date.toDisplay() + " " + minuteText(static_cast<int>(rest / 60));
}

std::string renderReceiptText(const Receipt& r) {
    std::string out;
    out += rule('=') + "\n";
    out += centred("HOSPITAL MANAGEMENT SYSTEM") + "\n";
    out += centred("PAYMENT RECEIPT") + "\n";
    out += rule('=') + "\n";
    out += labelled("Receipt no : " + r.receiptNumber, "Date : " + r.paidAtLocal) + "\n";
    out += "Bill no    : " + formatBillId(r.payment.billId) + "\n";
    out += "Patient    : " + formatUhid(r.uhid) + "  " + r.patientName + "\n";
    out += "Visit      : " + formatAppointmentId(r.appointmentId) + "  " + visitText(r.slotStart) + "  Dr " +
           r.doctorName + "\n";
    out += rule('-') + "\n";
    appendLines(out, r.lines);
    out += labelled("Bill total", "Rs " + formatAmount(r.billTotalPaise)) + "\n";
    out += labelled("Paid with this receipt (" + paymentModeLabel(r.payment.mode) + ")",
                    "Rs " + formatAmount(r.payment.amountPaise)) + "\n";
    if (r.payment.reference) out += "Reference  : " + *r.payment.reference + "\n";
    out += labelled("Total paid so far", "Rs " + formatAmount(r.paidToDatePaise)) + "\n";
    out += labelled("Balance after this payment", "Rs " + formatAmount(r.balanceAfterPaise)) + "\n";
    out += rule('-') + "\n";
    out += "Received by: " + r.payment.receivedBy + "\n";
    out += rule('=') + "\n";
    return out;
}

std::string renderBillText(const Bill& b, int utcOffsetMinutes) {
    auto local = [&](const std::string& iso) {
        return localDateTimeText(iso, utcOffsetMinutes);
    };
    std::string out;
    out += rule('=') + "\n";
    out += centred("HOSPITAL MANAGEMENT SYSTEM") + "\n";
    out += centred("BILL") + "\n";
    out += rule('=') + "\n";
    out += labelled("Bill no    : " + formatBillId(b.billId), "Date : " + local(b.createdAt)) + "\n";
    out += "Patient    : " + formatUhid(b.uhid) + "  " + b.patientName + "\n";
    out += "Visit      : " + formatAppointmentId(b.appointmentId) + "  " + visitText(b.slotStart) + "  Dr " +
           b.doctorName + "\n";
    out += rule('-') + "\n";
    appendLines(out, b.lines);
    out += labelled("Bill total", "Rs " + formatAmount(b.totalPaise)) + "\n";
    out += labelled("Paid", "Rs " + formatAmount(b.paidPaise)) + "\n";
    out += labelled("Outstanding balance (" + to_string(b.status()) + ")", "Rs " + formatAmount(b.balancePaise())) +
           "\n";
    if (!b.payments.empty()) {
        out += rule('-') + "\n";
        out += "Payments\n";
        for (const auto& p : b.payments) {
            out += labelled("  " + formatReceiptId(p.paymentId) + "  " + local(p.paidAt) + "  " +
                                paymentModeLabel(p.mode),
                            "Rs " + formatAmount(p.amountPaise)) + "\n";
        }
    }
    out += rule('=') + "\n";
    return out;
}

std::string writeTextFile(const std::string& path, const std::string& content) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file) return "The file '" + path + "' could not be created. Check that the folder exists.";
    file << content;
    file.flush();
    if (!file) return "The file '" + path + "' could not be written completely.";
    return "";
}

}  // namespace hms
