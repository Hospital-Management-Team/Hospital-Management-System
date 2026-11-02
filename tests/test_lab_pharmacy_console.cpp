// Issue #8: scripted console sessions for the lab and pharmacy menus and the
// Doctor's lab-results option, typed exactly as an operator would (STP 5.2,
// console-session tests).
#include <doctest.h>

#include <sstream>

#include "lab_pharmacy_test_support.hpp"
#include "record/medical_record_service.hpp"
#include "ui/console_app.hpp"
#include "ui/lab_pharmacy_screens.hpp"

using namespace hms;
using test::doctorSession;
using test::LabPharmacyFixture;
using test::labTech;
using test::pharmacistSession;

namespace {
std::string runLab(LabPharmacyFixture& f, const std::string& script) {
    std::istringstream in(script);
    std::ostringstream out;
    ConsoleIO io(in, out);
    labMenu(io, f.lab, labTech());
    return out.str();
}
std::string runPharmacy(LabPharmacyFixture& f, const std::string& script) {
    std::istringstream in(script);
    std::ostringstream out;
    ConsoleIO io(in, out);
    pharmacyMenu(io, f.pharmacy, pharmacistSession());
    return out.str();
}
std::string runDoctor(LabPharmacyFixture& f, const std::string& script, bool withLab = true) {
    std::istringstream in(script);
    std::ostringstream out;
    ConsoleIO io(in, out);
    ConsoleApp app(io, *f.service, *f.appointments, doctorSession("mehta"));
    app.setConsultationService(f.consultations);
    if (withLab) app.setLabService(f.lab);
    app.run();
    return out.str();
}
bool contains(const std::string& hay, const std::string& needle) { return hay.find(needle) != std::string::npos; }
}  // namespace

TEST_CASE("Console TC-Lab-01: the lab technician enters a result and updates it; the doctor reads it") {
    LabPharmacyFixture f;
    const auto doc = f.addDoctor("mehta");
    const auto uhid = f.addPatient("Ravi Kumar");
    f.seen(uhid, doc, "mehta", {"Complete Blood Count"});

    const std::string lab = runLab(f,
                                   "1\n"                       // tests waiting
                                   "2\n"
                                   "abc\n"                     // not a number -> asked again
                                   "LAB-000001\n"
                                   "Hb 13.2 g/dL\n"
                                   "1\n"                       // nothing is waiting any more
                                   "2\n"
                                   "1\n"
                                   "Hb 12.9 g/dL (repeat)\n"   // an update
                                   "3\n"
                                   "HMS-000001\n"
                                   "9\n"                       // not a menu option
                                   "0\n");
    CHECK(contains(lab, "TESTS WAITING FOR A RESULT"));
    CHECK(contains(lab, "Complete Blood Count"));
    CHECK(contains(lab, "Order number: enter the number as printed"));
    CHECK(contains(lab, "Saved as revision 1 of LAB-000001"));
    CHECK(contains(lab, "None. Every ordered test has a result."));
    CHECK(contains(lab, "Saved as revision 2 of LAB-000001"));
    CHECK(contains(lab, "Hb 12.9 g/dL (repeat)"));
    CHECK(contains(lab, "Please choose 0, 1, 2 or 3."));
    CHECK(f.count("SELECT count(*) FROM lab_result") == 2);

    const std::string doctor = runDoctor(f, "8\nHMS-000001\n0\n");
    CHECK(contains(doctor, "8. Lab results of a patient"));
    CHECK(contains(doctor, "LAB RESULTS of HMS-000001"));
    CHECK(contains(doctor, "Hb 12.9 g/dL (repeat)"));

    // Without the lab service the doctor menu has no option 8.
    const std::string plain = runDoctor(f, "8\n0\n", false);
    CHECK_FALSE(contains(plain, "Lab results of a patient"));
    CHECK(contains(plain, "Please choose a number from 0 to 7."));
}

TEST_CASE("Console TC-Lab-01: cancelling at the result prompt saves nothing") {
    LabPharmacyFixture f;
    const auto doc = f.addDoctor("mehta");
    f.seen(f.addPatient("Ravi Kumar"), doc, "mehta", {"CBC"});

    const std::string out = runLab(f, "2\n1\n\n0\n");
    CHECK(contains(out, "Cancelled. Nothing was saved."));
    CHECK(f.count("SELECT count(*) FROM lab_result") == 0);
    CHECK(contains(runLab(f, "2\n99\n0\n"), "No lab order found with number LAB-000099."));
}

TEST_CASE("Console TC-Pharm-01: the pharmacist adds, restocks and dispenses and sees the low-stock alert") {
    LabPharmacyFixture f;
    const auto doc = f.addDoctor("mehta");
    f.seen(f.addPatient("Ravi Kumar"), doc, "mehta", {}, {{"Paracetamol", "1 tablet twice a day", "5"}});

    const std::string out = runPharmacy(f,
                                        "1\n"                              // empty stock list
                                        "2\n"
                                        "Paracetamol 500 mg\n"
                                        "tablet\n"
                                        "12\n"                             // opening stock
                                        "3\n"
                                        "MED-000001\n"
                                        "abc\n"                            // refused, nothing changes
                                        "3\n"
                                        "MED-000001\n"
                                        "3\n"                              // 15 in stock now
                                        "4\n"
                                        "5\n"
                                        "RX-000001\n"
                                        "MED-000001\n"
                                        "\n"                               // default: the 5 still to dispense
                                        "Y\n"
                                        "6\n"
                                        "HMS-000001\n"
                                        "12\n"                            // not a menu option
                                        "0\n");
    CHECK(contains(out, "No medicine is in the stock list yet"));
    CHECK(contains(out, "Added Paracetamol 500 mg as MED-000001 with 12 tablet in stock."));
    CHECK(contains(out, "Quantity: must be"));
    CHECK(contains(out, "now has 15 tablet in stock."));
    CHECK(contains(out, "PRESCRIPTIONS WAITING TO BE DISPENSED"));
    CHECK(contains(out, "Dispensed 5 tablet of Paracetamol 500 mg. The line is complete. 10 tablet left in stock."));
    CHECK_FALSE(contains(out, "! LOW STOCK"));  // 10 is not below 10
    CHECK(contains(out, "PRESCRIPTIONS OF HMS-000001"));
    CHECK(contains(out, "5 of 5"));
    CHECK(contains(out, "Please choose a number from 0 to 9."));
    CHECK(f.stockOf(1) == 10);
}

TEST_CASE("Console TC-Pharm-01: a dispense that takes the stock below the threshold shows the alert; 'N' dispenses nothing") {
    LabPharmacyFixture f;
    const auto doc = f.addDoctor("mehta");
    f.seen(f.addPatient("Ravi Kumar"), doc, "mehta", {}, {{"Paracetamol", "daily", "6"}});
    f.addMedicine("Paracetamol 500 mg", "tablet", "12");

    const std::string declined = runPharmacy(f, "5\nRX-000001\nMED-000001\n6\nN\n0\n");
    CHECK(contains(declined, "Cancelled. Nothing was saved."));
    CHECK(f.stockOf(1) == 12);

    const std::string out = runPharmacy(f, "5\nRX-000001\nMED-000001\n6\nY\n1\n0\n");
    CHECK(contains(out, "! LOW STOCK: Paracetamol 500 mg is below 10. Please reorder."));
    CHECK(contains(out, "LOW STOCK"));
    CHECK(f.stockOf(1) == 6);

    const std::string none = runPharmacy(f, "4\n5\n0\n");
    CHECK(contains(none, "None. Every prescription line has been dispensed."));
    CHECK(contains(none, "Nothing is waiting to be dispensed."));
}

TEST_CASE("Console TC-Pharm-01: a refused dispense shows the reason and leaves the stock alone") {
    LabPharmacyFixture f;
    const auto doc = f.addDoctor("mehta");
    f.seen(f.addPatient("Ravi Kumar"), doc, "mehta", {}, {{"Paracetamol", "daily", "30"}});
    f.addMedicine("Paracetamol 500 mg", "tablet", "20");

    const std::string out = runPharmacy(f, "5\nRX-000001\nMED-000001\n30\nY\n0\n");
    CHECK(contains(out, "Only 20 tablet of Paracetamol 500 mg in stock. Nothing was dispensed."));
    CHECK(f.stockOf(1) == 20);
    CHECK(f.count("SELECT count(*) FROM dispense") == 0);
}

TEST_CASE("Console TC-Lab-01: with the medical history entry present, lab results are option 9") {
    LabPharmacyFixture f;
    const auto doc = f.addDoctor("mehta");
    const auto uhid = f.addPatient("Ravi Kumar");
    const auto c = f.seen(uhid, doc, "mehta", {"CBC"});
    REQUIRE(f.lab.recordResult(labTech(), c.labOrders[0].orderId, "Normal").ok());

    MedicalRecordService records(f.db(), f.clock, f.policy);
    std::istringstream in("9\nHMS-000001\n0\n");
    std::ostringstream out;
    ConsoleIO io(in, out);
    ConsoleApp app(io, *f.service, *f.appointments, doctorSession("mehta"));
    app.setConsultationService(f.consultations);
    app.setMedicalRecordService(records);
    app.setLabService(f.lab);
    app.run();
    CHECK(contains(out.str(), "8. Medical history"));
    CHECK(contains(out.str(), "9. Lab results of a patient"));
    CHECK(contains(out.str(), "LAB RESULTS of HMS-000001"));
    CHECK(contains(out.str(), "Normal"));
}

TEST_CASE("Console TC-Pharm-01: the pharmacist corrects a name, a stock count and the availability of a medicine") {
    LabPharmacyFixture f;
    f.addMedicine("Paracetamol 500 mg", "tablet", "30");

    const std::string out = runPharmacy(f,
                                        "7\n"
                                        "MED-000001\n"
                                        "Paracetamol 650 mg\n"
                                        "strip\n"
                                        "8\n"
                                        "MED-000001\n"
                                        "27\n"                           // counted on the shelf
                                        "\n"                              // no reason -> asked again by the service
                                        "8\n"
                                        "MED-000001\n"
                                        "27\n"
                                        "three strips damaged\n"
                                        "9\n"
                                        "MED-000001\n"                   // becomes unavailable
                                        "1\n"
                                        "9\n"
                                        "MED-000001\n"                   // and available again
                                        "0\n");
    CHECK(contains(out, "MED-000001 is now Paracetamol 650 mg (strip)."));
    CHECK(contains(out, "Reason: is required"));
    CHECK(contains(out, "Paracetamol 650 mg now has 27 strip in stock."));
    CHECK(contains(out, "UNAVAILABLE"));
    CHECK(contains(out, "Paracetamol 650 mg is now unavailable."));
    CHECK(contains(out, "Paracetamol 650 mg is now available."));
    CHECK(f.stockOf(1) == 27);
    CHECK(f.count("SELECT count(*) FROM stock_movement WHERE reason = 'CORRECTION' AND delta = -3") == 1);
}
