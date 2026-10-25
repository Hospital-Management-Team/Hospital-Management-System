// UHID display form and parsing (used by registration and search).
#include <doctest.h>

#include "patient/uhid.hpp"

using namespace hms;

TEST_CASE("UHID format and parsing") {
    CHECK(formatUhid(1) == "HMS-000001");
    CHECK(formatUhid(1234567) == "HMS-1234567");
    CHECK(parseUhid("HMS-000123").value() == 123);
    CHECK(parseUhid("hms-123").value() == 123);
    CHECK(parseUhid("HMS123").value() == 123);
    CHECK(parseUhid(" 42 ").value() == 42);
    CHECK_FALSE(parseUhid("0").has_value());
    CHECK_FALSE(parseUhid("HMS-").has_value());
    CHECK_FALSE(parseUhid("12a").has_value());
    CHECK_FALSE(parseUhid("-5").has_value());
}
