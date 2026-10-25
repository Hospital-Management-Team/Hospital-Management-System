// Foundation helpers: text handling, event IDs, calendar arithmetic, Result.
#include <doctest.h>

#include <set>

#include "core/clock.hpp"
#include "core/ids.hpp"
#include "core/result.hpp"
#include "core/text.hpp"

using namespace hms;

TEST_CASE("normaliseName trims, collapses spaces and folds ASCII case") {
    CHECK(text::normaliseName("  Ravi   KUMAR ") == "ravi kumar");
    CHECK(text::normaliseName("Ravi Kumar") == text::normaliseName("ravi  kumar"));
    // Non-ASCII text (no case) passes through unchanged.
    CHECK(text::normaliseName(u8"  रवि  ") == std::string(u8"रवि"));
}

TEST_CASE("UTF-8 validation and length in characters") {
    CHECK(text::isValidUtf8("plain"));
    CHECK(text::isValidUtf8(u8"रवि"));
    CHECK_FALSE(text::isValidUtf8("\xC3"));          // truncated sequence
    CHECK_FALSE(text::isValidUtf8("\xC0\xAF"));      // overlong
    CHECK_FALSE(text::isValidUtf8("\xED\xA0\x80"));  // surrogate
    CHECK(text::utf8Length(u8"रवि") == 3);
}

TEST_CASE("control characters are detected (C0, DEL, C1)") {
    CHECK(text::containsControlChars("a\tb"));
    CHECK(text::containsControlChars(std::string("a\0b", 3)));
    CHECK(text::containsControlChars("a\x7F"));
    CHECK(text::containsControlChars("a\xC2\x85"));  // U+0085 NEL
    CHECK_FALSE(text::containsControlChars(u8"é normal"));
}

TEST_CASE("fitWidth pads and cuts by characters") {
    CHECK(text::fitWidth("ab", 4) == "ab  ");
    CHECK(text::fitWidth("abcdefgh", 6) == "abc...");
    CHECK(text::utf8Length(text::fitWidth(u8"रविरविर", 5)) == 5);
}

TEST_CASE("event IDs are 32 hex digits and unique") {
    std::set<std::string> seen;
    for (int i = 0; i < 1000; ++i) {
        auto id = newEventId();
        REQUIRE(id.size() == 32);
        CHECK(id.find_first_not_of("0123456789abcdef") == std::string::npos);
        seen.insert(id);
    }
    CHECK(seen.size() == 1000);
}

TEST_CASE("calendar arithmetic and local date (Asia/Kolkata, UTC+05:30)") {
    CHECK(daysFromCivil(1970, 1, 1) == 0);
    CHECK(civilFromDays(daysFromCivil(2024, 2, 29)) == Date{2024, 2, 29});
    CHECK(toIsoUtc(makeUtc(2026, 10, 25, 6, 0, 0)) == "2026-10-25T06:00:00Z");
    // 18:29 UTC is 23:59 IST (same day); 18:30 UTC is 00:00 IST next day.
    CHECK(localDate(makeUtc(2026, 10, 25, 18, 29, 0), 330) == Date{2026, 10, 25});
    CHECK(localDate(makeUtc(2026, 10, 25, 18, 30, 0), 330) == Date{2026, 10, 26});
    CHECK(Date::fromIso("2026-02-29") == std::nullopt);
    CHECK(Date::fromIso("2024-02-29").value() == Date{2024, 2, 29});
    CHECK(Date{1990, 3, 5}.toDisplay() == "05-03-1990");
}

TEST_CASE("age in whole years") {
    const Date today{2026, 10, 25};
    CHECK(ageInYears(Date{1990, 10, 25}, today) == 36);  // birthday today
    CHECK(ageInYears(Date{1990, 10, 26}, today) == 35);  // birthday tomorrow
    CHECK(ageInYears(Date{2026, 10, 25}, today) == 0);
    CHECK(ageInYears(Date{2024, 2, 29}, Date{2025, 2, 28}) == 0);
    CHECK(ageInYears(Date{2024, 2, 29}, Date{2025, 3, 1}) == 1);
}

TEST_CASE("Result holds a value or one error that names every bad field") {
    Result<int> good = 5;
    CHECK(good.ok());
    CHECK(good.value() == 5);
    Result<int> bad = Error::validation({{"Name", "is required"}, {"Address", "is required"}});
    REQUIRE_FALSE(bad.ok());
    CHECK(bad.error().kind == ErrorKind::Validation);
    CHECK(bad.error().describe() == "Name: is required\nAddress: is required");
}
