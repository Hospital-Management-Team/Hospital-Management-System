// Input layer (SRS v11 draft section 3.1; task #13). Tested without the menu.
#include <doctest.h>

#include "validation/input.hpp"

using namespace hms;
using namespace hms::input;

TEST_CASE("cleanLine: trims spaces, enforces 1,000 characters, rejects control characters") {
    CHECK(cleanLine("  Ravi  ", "Name").value() == "Ravi");
    CHECK(cleanLine("", "Name").value().empty());

    CHECK(cleanLine(std::string(1000, 'a'), "Name").ok());
    auto tooLong = cleanLine(std::string(1001, 'a'), "Name");
    REQUIRE_FALSE(tooLong.ok());
    CHECK(tooLong.error().fields.at(0).field == "Name");
    CHECK(tooLong.error().fields.at(0).message.find("1,000") != std::string::npos);

    CHECK_FALSE(cleanLine("Ra\tvi", "Name").ok());
    CHECK_FALSE(cleanLine("\xFF\xFE", "Name").ok());  // not UTF-8
}

TEST_CASE("requireLine: an empty answer to a mandatory prompt is an error naming the field") {
    auto r = requireLine("   ", "Address");
    REQUIRE_FALSE(r.ok());
    CHECK(r.error().fields.at(0).field == "Address");
    CHECK(r.error().fields.at(0).message == "is required");
}

TEST_CASE("parseDate: DD-MM-YYYY with real calendar dates only") {
    CHECK(parseDate("05-03-1990", "Date").value() == Date{1990, 3, 5});
    CHECK(parseDate("5-3-1990", "Date").value() == Date{1990, 3, 5});
    CHECK(parseDate("29-02-2024", "Date").ok());   // leap year
    CHECK_FALSE(parseDate("29-02-2023", "Date").ok());
    CHECK_FALSE(parseDate("31-02-2026", "Date").ok());
    CHECK_FALSE(parseDate("31-04-2026", "Date").ok());
    CHECK_FALSE(parseDate("1990-03-05", "Date").ok());  // ISO order is not accepted
    CHECK_FALSE(parseDate("05/03/1990", "Date").ok());
    CHECK_FALSE(parseDate("05-03-90", "Date").ok());
    CHECK_FALSE(parseDate("aa-bb-cccc", "Date").ok());
    CHECK_FALSE(parseDate("05-03-1990-1", "Date").ok());
    auto impossible = parseDate("31-02-2026", "Date of birth");
    CHECK(impossible.error().fields.at(0).message.find("not a real calendar date") != std::string::npos);
}

TEST_CASE("parseWholeNumber: digits only, inside the range") {
    CHECK(parseWholeNumber("42", "Age", 0, 125).value() == 42);
    CHECK(parseWholeNumber("0", "Age", 0, 125).value() == 0);
    CHECK_FALSE(parseWholeNumber("126", "Age", 0, 125).ok());
    CHECK_FALSE(parseWholeNumber("-1", "Age", 0, 125).ok());
    CHECK_FALSE(parseWholeNumber("4 2", "Age", 0, 125).ok());
    CHECK_FALSE(parseWholeNumber("4.0", "Age", 0, 125).ok());
    CHECK_FALSE(parseWholeNumber("", "Age", 0, 125).ok());
    CHECK_FALSE(parseWholeNumber("99999999999999999999", "Age", 0, 125).ok());  // no overflow
}

TEST_CASE("parseMoneyPaise: the SRS 3.1 money format") {
    const std::int64_t max = 100000LL * 100;  // 1,00,000 rupees
    CHECK(parseMoneyPaise("10", "Amount", true, max).value() == 1000);
    CHECK(parseMoneyPaise("10.5", "Amount", true, max).value() == 1050);
    CHECK(parseMoneyPaise("10.50", "Amount", true, max).value() == 1050);
    CHECK(parseMoneyPaise("0.05", "Amount", true, max).value() == 5);
    for (const char* bad : {"10.555", "10.", ".50", "-10", "1e3", "", "10,00", " 10"}) {
        CAPTURE(bad);
        CHECK_FALSE(parseMoneyPaise(bad, "Amount", true, max).ok());
    }
    CHECK_FALSE(parseMoneyPaise("0", "Amount", true, max).ok());   // must be positive
    CHECK(parseMoneyPaise("0", "Price", false, max).value() == 0);  // zero allowed for a price
    CHECK_FALSE(parseMoneyPaise("100000.01", "Price", false, max).ok());
}
