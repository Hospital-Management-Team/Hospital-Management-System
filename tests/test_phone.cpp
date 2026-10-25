// TC-Reg-01 (phone part): canonical form of SRS v11 draft section 1.4, boundary
// cases listed in STP v1.10 section 5.2.
#include <doctest.h>

#include "patient/phone.hpp"

using namespace hms;

TEST_CASE("TC-Reg-01 phone: every accepted format gives the same 10 digits") {
    for (const char* ok : {"9876543210", "98765 43210", "98765-43210", "(98765) 43210", "98.765.432.10",
                           "09876543210", "098765-43210", "919876543210", "91 98765 43210", "+919876543210",
                           "+91 98765 43210", "+91-98765-43210"}) {
        CAPTURE(ok);
        auto r = phone::canonicalise(ok);
        REQUIRE(r.ok());
        CHECK(r.value() == "9876543210");
    }
}

TEST_CASE("TC-Reg-01 phone: first digit 6, 7, 8 and 9 accepted; 0-5 rejected") {
    CHECK(phone::canonicalise("6000000000").ok());
    CHECK(phone::canonicalise("7000000000").ok());
    CHECK(phone::canonicalise("8000000000").ok());
    CHECK(phone::canonicalise("9000000000").ok());
    CHECK_FALSE(phone::canonicalise("5999999999").ok());
    CHECK_FALSE(phone::canonicalise("0999999999").ok());
    CHECK_FALSE(phone::canonicalise("05999999999").ok());   // 0 + number starting with 5
    CHECK_FALSE(phone::canonicalise("915999999999").ok());  // 91 + number starting with 5
}

TEST_CASE("TC-Reg-01 phone: wrong lengths, prefixes, letters and landlines rejected") {
    for (const char* bad : {"987654321",       // 9 digits
                            "98765432101",     // 11 digits, no leading 0
                            "929876543210",    // 12 digits, not 91
                            "+9198765432",     // +91 + 9 digits
                            "+91987654321012", // +91 + 11 digits
                            "+9876543210",     // + not followed by 91
                            "9876+543210",     // + in the middle
                            "++919876543210",
                            "98765abcde", "080-2345678", "+1 415 555 0100", "", "   ", "+"}) {
        CAPTURE(bad);
        auto r = phone::canonicalise(bad);
        REQUIRE_FALSE(r.ok());
        CHECK(r.error().fields.at(0).field == "Mobile number");
    }
}

TEST_CASE("phone display form") { CHECK(phone::display("9876543210") == "+91 98765 43210"); }
