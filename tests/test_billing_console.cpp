// Issue #7: scripted console sessions for the billing screens, typed exactly as
// an operator would (STP 5.2, console-session tests).
#include <doctest.h>

#include <filesystem>
#include <fstream>
#include <sstream>

#include "billing_test_support.hpp"
#include "ui/billing_screens.hpp"
#include "ui/console_app.hpp"

using namespace hms;
using test::admin;
using test::BillingFixture;
using test::doctorSession;
using test::receptionist;

namespace {
std::string runReception(BillingFixture& f, const std::string& script, bool billing = true) {
    std::istringstream in(script);
    std::ostringstream out;
    ConsoleIO io(in, out);
    ConsoleApp app(io, *f.service, *f.appointments, receptionist());
    if (billing) app.setBillingService(f.billing);
    app.run();
    return out.str();
}
bool contains(const std::string& hay, const std::string& needle) { return hay.find(needle) != std::string::npos; }
}  // namespace

TEST_CASE("Console TC-Bill-01..03: reception bills a visit, takes a payment and prints the receipt") {
    BillingFixture f;
    const auto doc = f.addDoctor("mehta");
    f.setFee(doc, "500");
    const auto appt = f.completedVisit(f.addPatient("Ravi Kumar"), doc, "mehta", {"CBC"});

    const std::string out = runReception(f,
                                         "8\n"
                                         "apt-x\n"                                // not a number -> asked again
                                         + formatAppointmentId(appt.id) + "\n"
                                         "abc\n"                                  // bad test price -> asked again
                                         "350\n"
                                         "Y\n"                                    // another service charge
                                         "Dressing\n"
                                         "\n"                                     // quantity 1
                                         "75\n"
                                         "N\n"
                                         "Y\n"                                    // create the bill
                                         "9\n"
                                         "BILL-000001\n"
                                         "425\n"
                                         "1\n"                                    // cash
                                         "\n"                                     // no reference
                                         "N\n"                                    // no file
                                         "10\n"
                                         "\n"                                     // every bill with a balance
                                         "\n"                                     // do not open one
                                         "11\n"
                                         "RCPT-000001\n"
                                         "N\n"
                                         "0\n");
    CHECK(contains(out, "8. Bill a completed visit"));
    CHECK(contains(out, "11. Print a receipt again"));
    CHECK(contains(out, "Appointment: enter the number as printed"));
    CHECK(contains(out, "Consultation fee: Rs 500.00"));
    CHECK(contains(out, "Price: must be"));
    CHECK(contains(out, "Bill BILL-000001 created."));
    CHECK(contains(out, "Rs 925.00"));
    CHECK(contains(out, "Payment of Rs 425.00 recorded as RCPT-000001. Balance: Rs 500.00 (Partly paid)."));
    CHECK(contains(out, "PAYMENT RECEIPT"));
    CHECK(contains(out, "26-10-2026 09:30"));
    CHECK(contains(out, "Bills with a balance to pay"));
    CHECK(f.count("SELECT count(*) FROM bill") == 1);
    CHECK(f.count("SELECT count(*) FROM payment") == 1);
    CHECK(f.count("SELECT count(*) FROM audit_entry WHERE action = 'RECEIPT_GENERATE' AND outcome = 'OK'") == 2);
}

TEST_CASE("Console TC-Bill-03: the receipt can be saved as a text file") {
    BillingFixture f;
    const auto doc = f.addDoctor("mehta");
    f.setFee(doc, "500");
    const Bill b = f.billOk(f.completedVisit(f.addPatient("Ravi Kumar"), doc, "mehta").id);
    const auto dir = test::uniqueTempDir();
    const std::string path = (dir / "receipt.txt").string();

    const std::string out = runReception(f,
                                         "9\n" + formatBillId(b.billId) + "\n"
                                         "500\n"
                                         "3\n"                                    // insurance
                                         "CLAIM-42\n"
                                         "Y\n"
                                         + path + "\n"
                                         "0\n");
    CHECK(contains(out, "Saved to " + path));
    std::ifstream file(path);
    REQUIRE(file.good());
    std::stringstream body;
    body << file.rdbuf();
    CHECK(contains(body.str(), "RCPT-000001"));
    CHECK(contains(body.str(), "Insurance"));
    CHECK(contains(body.str(), "CLAIM-42"));
    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
}

TEST_CASE("Console TC-Bill-02: a refused payment is shown and nothing is saved") {
    BillingFixture f;
    const auto doc = f.addDoctor("mehta");
    f.setFee(doc, "500");
    const Bill b = f.billOk(f.completedVisit(f.addPatient("Ravi Kumar"), doc, "mehta").id);
    const std::string out = runReception(f,
                                         "9\n" + formatBillId(b.billId) + "\n"
                                         "900\n"                                  // more than the balance
                                         "1\n"
                                         "\n"
                                         "0\n");
    CHECK(contains(out, "more than the outstanding balance"));
    CHECK(f.count("SELECT count(*) FROM payment") == 0);
}

TEST_CASE("Console: a visit that is not complete cannot be billed from the menu") {
    BillingFixture f;
    const auto doc = f.addDoctor("mehta");
    f.setFee(doc, "500");
    const auto appt = f.bookOk(f.addPatient("Ravi Kumar"), doc, test::at(test::kTuesday, 9, 0));
    const std::string out = runReception(f, "8\n" + formatAppointmentId(appt.id) + "\n0\n");
    CHECK(contains(out, "the doctor must record the consultation"));
    CHECK(f.count("SELECT count(*) FROM bill") == 0);
}

TEST_CASE("Console: the billing entries appear only when billing is set up, and never for a Doctor") {
    BillingFixture f;
    const std::string without = runReception(f, "9\n0\n", false);
    CHECK_FALSE(contains(without, "Bill a completed visit"));
    CHECK(contains(without, "Please choose a number from 0 to 7."));
    const std::string with = runReception(f, "12\n0\n");
    CHECK(contains(with, "Please choose a number from 0 to 11."));

    f.addDoctor("mehta");
    std::istringstream in("0\n");
    std::ostringstream out;
    ConsoleIO io(in, out);
    ConsoleApp app(io, *f.service, *f.appointments, doctorSession("mehta"));
    app.setBillingService(f.billing);
    app.run();
    CHECK_FALSE(contains(out.str(), "Bill a completed visit"));
}

TEST_CASE("Console TC-Bill-01: the Admin sets a doctor's consultation fee") {
    BillingFixture f;
    f.addDoctor("mehta");
    f.addDoctor("rao");
    std::istringstream in("2\nabc\n450.50\n");
    std::ostringstream out;
    ConsoleIO io(in, out);
    consultationFeeScreen(io, f.billing, admin());
    CHECK(contains(out.str(), "mehta"));
    CHECK(contains(out.str(), "not set"));
    CHECK(contains(out.str(), "Consultation fee"));            // the error for "abc"
    auto fees = f.billing.listDoctorFees(admin());
    REQUIRE(fees.ok());
    CHECK(fees.value()[1].username == "rao");
    // "abc" was refused, so the second attempt needs its own session.
    std::istringstream in2("2\n450.50\n");
    std::ostringstream out2;
    ConsoleIO io2(in2, out2);
    consultationFeeScreen(io2, f.billing, admin());
    CHECK(contains(out2.str(), "Fee for Dr rao is now Rs 450.50."));
    CHECK(f.billing.listDoctorFees(admin()).value()[1].feePaise == 45050);
}

TEST_CASE("Console: a non-Admin cannot use the fee screen") {
    BillingFixture f;
    f.addDoctor("mehta");
    std::istringstream in("1\n100\n");
    std::ostringstream out;
    ConsoleIO io(in, out);
    consultationFeeScreen(io, f.billing, receptionist());
    CHECK(contains(out.str(), "may not manage consultation fees"));
}
