// Console input layer (issue #13): prompts, re-asking on invalid input,
// end of input cancels, Y/N answers.
#include <doctest.h>

#include <sstream>

#include "ui/console_io.hpp"
#include "validation/input.hpp"

using namespace hms;

TEST_CASE("ask re-prompts until the answer is valid and names the field in each error") {
    std::istringstream in("abc\n200\n42\n");
    std::ostringstream out;
    ConsoleIO io(in, out);
    auto age = io.ask<std::int64_t>("Age: ", [](const std::string& s) { return input::parseWholeNumber(s, "Age", 0, 125); });
    REQUIRE(age.has_value());
    CHECK(*age == 42);
    CHECK(out.str().find("Age: must be a whole number") != std::string::npos);
    CHECK(out.str().find("Age: must be between 0 and 125") != std::string::npos);
}

TEST_CASE("end of input cancels the question; repeated end of input marks the input finished") {
    std::istringstream in("");
    std::ostringstream out;
    ConsoleIO io(in, out);
    CHECK_FALSE(io.readLine("x: ").has_value());
    CHECK_FALSE(io.askYesNo("ok? ").has_value());
    CHECK_FALSE(io.readLine("y: ").has_value());
    CHECK(io.exhausted());
}

TEST_CASE("Windows line endings are accepted and Y/N answers are case-insensitive") {
    std::istringstream in("maybe\r\nYES\r\nn\n");
    std::ostringstream out;
    ConsoleIO io(in, out);
    CHECK(io.askYesNo("? ").value() == true);
    CHECK(io.askYesNo("? ").value() == false);
    CHECK(out.str().find("Please answer Y or N.") != std::string::npos);
}
