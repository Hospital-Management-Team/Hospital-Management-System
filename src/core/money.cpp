#include "core/money.hpp"

#include <cstdio>

namespace hms {

std::string formatAmount(std::int64_t paise) {
    const bool negative = paise < 0;
    const std::int64_t abs = negative ? -paise : paise;
    std::string rupees = std::to_string(abs / 100);
    for (int i = static_cast<int>(rupees.size()) - 3; i > 0; i -= 3) rupees.insert(static_cast<std::size_t>(i), ",");
    char tail[8];
    std::snprintf(tail, sizeof tail, ".%02d", static_cast<int>(abs % 100));
    return std::string(negative ? "-" : "") + rupees + tail;
}

std::string formatRupees(std::int64_t paise) { return "Rs " + formatAmount(paise); }

}  // namespace hms
