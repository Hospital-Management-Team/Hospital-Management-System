#include "billing/billing.hpp"

#include "core/text.hpp"

namespace hms {

std::string to_string(PaymentMode m) {
    switch (m) {
        case PaymentMode::Cash: return "CASH";
        case PaymentMode::Card: return "CARD";
        case PaymentMode::Insurance: return "INSURANCE";
    }
    return "CASH";
}

std::string paymentModeLabel(PaymentMode m) {
    switch (m) {
        case PaymentMode::Cash: return "Cash";
        case PaymentMode::Card: return "Card";
        case PaymentMode::Insurance: return "Insurance";
    }
    return "Cash";
}

std::optional<PaymentMode> paymentModeFromStorage(const std::string& s) {
    if (s == "CASH") return PaymentMode::Cash;
    if (s == "CARD") return PaymentMode::Card;
    if (s == "INSURANCE") return PaymentMode::Insurance;
    return std::nullopt;
}

std::string to_string(BillStatus s) {
    switch (s) {
        case BillStatus::Unpaid: return "Unpaid";
        case BillStatus::PartiallyPaid: return "Partly paid";
        case BillStatus::Paid: return "Paid";
    }
    return "Unpaid";
}

namespace {
std::string numbered(const char* prefix, std::int64_t id) {
    std::string digits = std::to_string(id);
    if (digits.size() < 6) digits.insert(0, 6 - digits.size(), '0');
    return std::string(prefix) + "-" + digits;
}

std::optional<std::int64_t> parseNumbered(std::string_view raw, const char* prefix) {
    std::string t = text::asciiLower(text::trimSpaces(std::string(raw)));
    const std::string p = text::asciiLower(prefix) + "-";
    if (t.rfind(p, 0) == 0) t = t.substr(p.size());
    if (t.empty() || t.size() > 12) return std::nullopt;
    std::int64_t n = 0;
    for (char c : t) {
        if (c < '0' || c > '9') return std::nullopt;
        n = n * 10 + (c - '0');
    }
    if (n <= 0) return std::nullopt;
    return n;
}
}  // namespace

std::string formatBillId(std::int64_t id) { return numbered("BILL", id); }
std::string formatReceiptId(std::int64_t id) { return numbered("RCPT", id); }
std::optional<std::int64_t> parseBillId(std::string_view text) { return parseNumbered(text, "BILL"); }
std::optional<std::int64_t> parseReceiptId(std::string_view text) { return parseNumbered(text, "RCPT"); }

}  // namespace hms
