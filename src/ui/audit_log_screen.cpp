#include "ui/audit_log_screen.hpp"

#include <ostream>
#include <string>

#include "billing/receipt.hpp"
#include "core/text.hpp"
#include "validation/input.hpp"

namespace hms {

namespace {
// The store keeps fixed words (SRS HMS-SR-004); STARTED and PREPARED are shown in full.
std::string outcomeText(const std::string& stored) {
    if (stored == "STARTED") return "started, outcome not recorded";
    if (stored == "PREPARED") return "prepared, outcome not recorded";
    return stored;
}
}  // namespace

void auditLogScreen(ConsoleIO& io, AuditLogService& auditLog, const Session& session) {
    auto& out = io.out();
    const auto& view = auditLog.policy().auditView;
    const std::string prompt = "How many of the newest entries? (1 to " + std::to_string(view.maxEntries) +
                               ", Enter for " + std::to_string(view.defaultEntries) + "): ";
    auto count = io.ask<std::int64_t>(prompt, [&](const std::string& s) -> Result<std::int64_t> {
        const std::string typed = text::trimSpaces(s);
        if (typed.empty()) return static_cast<std::int64_t>(view.defaultEntries);
        return input::parseWholeNumber(typed, "Number of entries", 1, static_cast<std::int64_t>(view.maxEntries));
    });
    if (!count) return;

    auto r = auditLog.recentEntries(session, static_cast<std::size_t>(*count));
    if (!r) { io.showError(r.error()); return; }
    const int offset = auditLog.policy().utcOffsetMinutes;
    out << "\nAUDIT LOG (newest first, read only)\n";
    if (r.value().empty()) {
        out << "  No entries.\n";
        return;
    }
    // fitWidth adds no padding when the text is exactly the column width, so the
    // action column is wider than the longest action name (30 characters).
    constexpr std::size_t kActionWidth = 32;
    out << "  " << text::fitWidth("Time", 18) << text::fitWidth("User", 14) << text::fitWidth("Action", kActionWidth)
        << text::fitWidth("Record", 14) << "Outcome\n";
    for (const auto& e : r.value()) {
        out << "  " << text::fitWidth(localDateTimeText(e.recordedAt, offset), 18) << text::fitWidth(e.userId, 14)
            << text::fitWidth(e.action, kActionWidth) << text::fitWidth(e.recordId.value_or("-"), 14)
            << outcomeText(e.outcome)
            << "\n";
        if (e.changedFields) out << "      fields: " << *e.changedFields << "\n";
        if (e.reasonCode) out << "      reason: " << *e.reasonCode << "\n";
    }
    out << "  " << r.value().size() << " entr" << (r.value().size() == 1 ? "y" : "ies") << " shown.\n";
}

}  // namespace hms
