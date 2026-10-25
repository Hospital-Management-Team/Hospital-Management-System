#include "ui/console_io.hpp"

#include <istream>
#include <ostream>

#include "core/text.hpp"

namespace hms {

std::optional<std::string> ConsoleIO::readLine(const std::string& prompt) {
    if (exhausted_) return std::nullopt;
    out_ << prompt << std::flush;
    std::string line;
    if (!std::getline(in_, line)) {
        out_ << "\n";
        // A terminal can continue after Ctrl-D / Ctrl-Z, so clear the state.
        // A file or pipe keeps returning end of input; after a few in a row
        // the input is treated as finished so no menu can loop forever.
        in_.clear();
        if (++consecutiveEof_ >= 3) exhausted_ = true;
        return std::nullopt;
    }
    consecutiveEof_ = 0;
    if (!line.empty() && line.back() == '\r') line.pop_back();
    return line;
}

std::optional<bool> ConsoleIO::askYesNo(const std::string& prompt) {
    while (true) {
        auto line = readLine(prompt);
        if (!line) return std::nullopt;
        const std::string v = text::asciiLower(text::trimSpaces(*line));
        if (v == "y" || v == "yes") return true;
        if (v == "n" || v == "no") return false;
        out_ << "  Please answer Y or N.\n";
    }
}

void ConsoleIO::showError(const Error& e) {
    if (e.fields.empty()) {
        out_ << "  ! " << e.message << "\n";
        return;
    }
    for (const auto& f : e.fields) out_ << "  ! " << f.field << ": " << f.message << "\n";
}

}  // namespace hms
