#pragma once
// Console input/output for the menu-driven UI (Login / Role Menu UI, SAD 3.4).
// Takes any istream/ostream, so whole console sessions can be scripted in
// tests. Contains no business rules.

#include <functional>
#include <iosfwd>
#include <optional>
#include <string>

#include "core/result.hpp"

namespace hms {

class ConsoleIO {
public:
    ConsoleIO(std::istream& in, std::ostream& out) : in_(in), out_(out) {}

    // Shows the prompt and reads one line. Returns nullopt at end of input
    // (Ctrl-D on Linux, Ctrl-Z then Enter on Windows), which cancels the
    // current operation (SRS 3.1). Removes a trailing '\r'.
    std::optional<std::string> readLine(const std::string& prompt);

    // Asks until `parse` accepts the answer. Each error names the field and
    // the rule. Returns nullopt if the user cancels with end of input.
    template <typename T>
    std::optional<T> ask(const std::string& prompt, const std::function<Result<T>(const std::string&)>& parse) {
        while (true) {
            auto line = readLine(prompt);
            if (!line) return std::nullopt;
            auto r = parse(*line);
            if (r) return std::move(r).value();
            showError(r.error());
        }
    }

    // Y/N question. Returns nullopt on end of input.
    std::optional<bool> askYesNo(const std::string& prompt);

    void showError(const Error& e);
    std::ostream& out() { return out_; }

    // True once end of input has been seen on a stream that cannot recover
    // (for example a script file). Used to leave the menus cleanly.
    bool exhausted() const { return exhausted_; }

private:
    std::istream& in_;
    std::ostream& out_;
    bool exhausted_ = false;
    int consecutiveEof_ = 0;
};

}  // namespace hms
