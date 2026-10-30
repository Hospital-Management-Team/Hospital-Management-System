#pragma once
// Money is held as whole paise in a 64-bit integer (SRS 3.1, input::parseMoneyPaise)
// so totals and balances are exact; floating point is never used for amounts.

#include <cstdint>
#include <string>

namespace hms {

// "1,234.50" - digit grouping in thousands, always two decimals.
std::string formatAmount(std::int64_t paise);

// "Rs 1,234.50" - plain ASCII so every console code page prints it.
std::string formatRupees(std::int64_t paise);

}  // namespace hms
