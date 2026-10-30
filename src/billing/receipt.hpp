#pragma once
// Printable / exportable text for a bill and a receipt (SRS HMS-F-032,
// "Optional export interface to generate reports/receipts as text"). Pure
// formatting: no database access, so it is easy to test.

#include <string>

#include "billing/billing.hpp"

namespace hms {

// A stored UTC time ("2026-10-25T07:54:00Z") as hospital-local "DD-MM-YYYY HH:MM".
std::string localDateTimeText(const std::string& isoUtc, int utcOffsetMinutes);

// Receipt text, 78 columns wide, ready to print or save.
std::string renderReceiptText(const Receipt& receipt);

// Itemised bill with its payments, for the screen and for printing.
// `utcOffsetMinutes` converts stored UTC times to hospital-local time.
std::string renderBillText(const Bill& bill, int utcOffsetMinutes);

// Writes text to a file (UTF-8, overwriting). Returns an empty string on
// success, otherwise a message for the user.
std::string writeTextFile(const std::string& path, const std::string& content);

}  // namespace hms
