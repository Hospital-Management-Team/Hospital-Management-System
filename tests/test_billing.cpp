// TC-Bill-01 (itemised bill), TC-Bill-02 (payments and balance) and TC-Bill-03
// (receipt) for issue #7 (SRS HMS-F-030..032). Integration tests against a real
// SQLite store.
#include <doctest.h>

#include <string>

#include "billing/receipt.hpp"
#include "billing_test_support.hpp"

using namespace hms;
using test::admin;
using test::at;
using test::BillingFixture;
using test::doctorSession;
using test::kMonday;
using test::kTuesday;
using test::receptionist;

TEST_CASE("TC-Bill-01 the bill lists the consultation, each test and other charges, and the total is their sum") {
    BillingFixture f;
    const auto doc = f.addDoctor("mehta");
    f.setFee(doc, "500");
    const auto appt = f.completedVisit(f.addPatient("Ravi Kumar"), doc, "mehta", {"Complete Blood Count", "Chest X-ray"});

    auto r = f.billing.generateBill(receptionist(), appt.id, {"350", "0"}, {{"Dressing", "2", "75.50"}});
    REQUIRE(r.ok());
    const Bill& b = r.value();
    CHECK(formatBillId(b.billId) == "BILL-000001");
    REQUIRE(b.lines.size() == 4);
    CHECK(b.lines[0].kind == "CONSULTATION");
    CHECK(b.lines[0].amountPaise == 50000);
    CHECK(b.lines[1].description == "Test - Complete Blood Count");
    CHECK(b.lines[1].amountPaise == 35000);
    CHECK(b.lines[2].amountPaise == 0);
    CHECK(b.lines[3].kind == "SERVICE");
    CHECK(b.lines[3].quantity == 2);
    CHECK(b.lines[3].amountPaise == 15100);
    std::int64_t sum = 0;
    for (const auto& l : b.lines) sum += l.amountPaise;
    CHECK(b.totalPaise == sum);
    CHECK(b.totalPaise == 100100);
    CHECK(b.balancePaise() == 100100);
    CHECK(b.status() == BillStatus::Unpaid);
    CHECK(b.uhid == appt.uhid);
    CHECK(b.appointmentId == appt.id);
    CHECK(b.doctorName == "mehta");
}

TEST_CASE("TC-Bill-01 every bill has its own number") {
    BillingFixture f;
    const auto doc = f.addDoctor("mehta");
    f.setFee(doc, "400");
    const auto first = f.completedVisit(f.addPatient("Ravi Kumar"), doc, "mehta");
    const auto second = f.completedVisit(f.addPatient("Anita Rao"), doc, "mehta", {}, at(kMonday, 10, 0));
    const Bill a = f.billOk(first.id);
    const Bill b = f.billOk(second.id);
    CHECK(a.billId != b.billId);
    CHECK(formatBillId(b.billId) == "BILL-000002");
    CHECK(f.count("SELECT count(DISTINCT bill_id) FROM bill") == 2);
}

TEST_CASE("TC-Bill-01 the preview shows the fee and the tests reception has to price") {
    BillingFixture f;
    const auto doc = f.addDoctor("mehta");
    f.setFee(doc, "500");
    const auto appt = f.completedVisit(f.addPatient("Ravi Kumar"), doc, "mehta", {"CBC", "ECG"});
    auto p = f.billing.previewBill(receptionist(), appt.id);
    REQUIRE(p.ok());
    CHECK(p.value().consultationFeePaise == 50000);
    REQUIRE(p.value().tests.size() == 2);
    CHECK(p.value().tests[1] == "ECG");
    CHECK(p.value().patientName == "Ravi Kumar");
}

TEST_CASE("TC-Bill-01 invalid prices and service lines are rejected and nothing is saved") {
    BillingFixture f;
    const auto doc = f.addDoctor("mehta");
    f.setFee(doc, "500");
    const auto appt = f.completedVisit(f.addPatient("Ravi Kumar"), doc, "mehta", {"CBC"});

    auto bad = f.billing.generateBill(receptionist(), appt.id, {"12.345"}, {{"", "0", "-5"}});
    REQUIRE_FALSE(bad.ok());
    CHECK(bad.error().kind == ErrorKind::Validation);
    CHECK(bad.error().fields.size() >= 4);  // every bad field is reported together
    auto wrongCount = f.billing.generateBill(receptionist(), appt.id, {});
    REQUIRE_FALSE(wrongCount.ok());
    CHECK(wrongCount.error().kind == ErrorKind::Validation);
    CHECK(f.count("SELECT count(*) FROM bill") == 0);
    CHECK(f.count("SELECT count(*) FROM bill_line") == 0);

    std::vector<ExtraChargeDraft> many(21, ExtraChargeDraft{"Item", "1", "10"});
    CHECK(f.billing.generateBill(receptionist(), appt.id, {"100"}, many).error().kind == ErrorKind::Validation);
    CHECK(f.billOk(appt.id, {"100"}).totalPaise == 60000);  // the corrected entry works
}

TEST_CASE("TC-Bill-01 only a finished, uncancelled visit with a fee set can be billed, and only once") {
    BillingFixture f;
    const auto doc = f.addDoctor("mehta");
    const auto uhid = f.addPatient("Ravi Kumar");

    CHECK(f.billing.generateBill(receptionist(), 999).error().kind == ErrorKind::NotFound);

    const auto booked = f.bookOk(uhid, doc, at(kTuesday, 9, 0));  // booked, not yet held
    auto early = f.billing.generateBill(receptionist(), booked.id);
    REQUIRE_FALSE(early.ok());
    CHECK(early.error().kind == ErrorKind::Conflict);

    const auto done = f.completedVisit(uhid, doc, "mehta");
    auto noFee = f.billing.generateBill(receptionist(), done.id);  // the Admin has not set a fee
    REQUIRE_FALSE(noFee.ok());
    CHECK(noFee.error().kind == ErrorKind::Conflict);
    CHECK(noFee.error().message.find("consultation fee") != std::string::npos);

    f.setFee(doc, "300");
    REQUIRE(f.billing.generateBill(receptionist(), done.id).ok());
    auto twice = f.billing.generateBill(receptionist(), done.id);
    REQUIRE_FALSE(twice.ok());
    CHECK(twice.error().kind == ErrorKind::Conflict);
    CHECK(f.count("SELECT count(*) FROM bill") == 1);
}

TEST_CASE("TC-Bill-01 a cancelled appointment cannot be billed") {
    BillingFixture f;
    const auto doc = f.addDoctor("mehta");
    f.setFee(doc, "300");
    const auto appt = f.bookOk(f.addPatient("Ravi Kumar"), doc, at(kTuesday, 9, 0));
    REQUIRE(f.appointments->cancel(receptionist(), appt.id).ok());
    auto r = f.billing.generateBill(receptionist(), appt.id);
    REQUIRE_FALSE(r.ok());
    CHECK(r.error().kind == ErrorKind::Conflict);
}

TEST_CASE("TC-Bill-01 the Admin sets the consultation fee; others cannot; old bills keep their fee") {
    BillingFixture f;
    const auto doc = f.addDoctor("mehta");
    const auto noProfile = f.addStaff("rao");

    auto listed = f.billing.listDoctorFees(admin());
    REQUIRE(listed.ok());
    REQUIRE(listed.value().size() == 1);
    CHECK_FALSE(listed.value()[0].feePaise.has_value());

    CHECK(f.billing.setConsultationFee(admin(), doc, "abc").error().kind == ErrorKind::Validation);
    CHECK(f.billing.setConsultationFee(admin(), doc, "").error().kind == ErrorKind::Validation);
    CHECK(f.billing.setConsultationFee(admin(), noProfile, "100").error().kind == ErrorKind::NotFound);
    CHECK(f.billing.setConsultationFee(receptionist(), doc, "100").error().kind == ErrorKind::AccessDenied);
    CHECK(f.billing.setConsultationFee(doctorSession("mehta"), doc, "100").error().kind == ErrorKind::AccessDenied);
    CHECK(f.billing.listDoctorFees(receptionist()).error().kind == ErrorKind::AccessDenied);
    CHECK(f.count("SELECT count(*) FROM audit_entry WHERE action = 'CONSULTATION_FEE_SET' AND outcome = 'DENIED'") == 3);

    f.setFee(doc, "500");
    const auto first = f.completedVisit(f.addPatient("Ravi Kumar"), doc, "mehta");
    const Bill old = f.billOk(first.id);
    f.setFee(doc, "650.50");
    CHECK(f.billing.getBill(receptionist(), old.billId).value().totalPaise == 50000);
    const auto second = f.completedVisit(f.addPatient("Anita Rao"), doc, "mehta", {}, at(kMonday, 10, 0));
    CHECK(f.billOk(second.id).totalPaise == 65050);
    CHECK(f.count("SELECT count(*) FROM audit_entry WHERE action = 'CONSULTATION_FEE_SET' AND outcome = 'OK'") == 2);
}

TEST_CASE("TC-Bill-01 only the Receptionist bills; a denial is audited") {
    BillingFixture f;
    const auto doc = f.addDoctor("mehta");
    f.setFee(doc, "500");
    const auto appt = f.completedVisit(f.addPatient("Ravi Kumar"), doc, "mehta");
    CHECK(f.billing.generateBill(doctorSession("mehta"), appt.id).error().kind == ErrorKind::AccessDenied);
    CHECK(f.billing.generateBill(admin(), appt.id).error().kind == ErrorKind::AccessDenied);
    CHECK(f.billing.previewBill(doctorSession("mehta"), appt.id).error().kind == ErrorKind::AccessDenied);
    CHECK(f.billing.getBill(doctorSession("mehta"), 1).error().kind == ErrorKind::AccessDenied);
    CHECK(f.count("SELECT count(*) FROM bill") == 0);
    CHECK(f.count("SELECT count(*) FROM audit_entry WHERE action = 'BILL_GENERATE' AND outcome = 'DENIED'") == 3);
}

TEST_CASE("TC-Bill-01 the audit entries hold IDs and field names only") {
    BillingFixture f;
    const auto doc = f.addDoctor("mehta");
    f.setFee(doc, "500");
    const auto appt = f.completedVisit(f.addPatient("Ravi Kumar"), doc, "mehta", {"Lipid Profile"});
    const Bill b = f.billOk(appt.id, {"900"}, {{"Dressing", "1", "40"}});
    REQUIRE(f.billing.recordPayment(receptionist(), b.billId, "100", PaymentMode::Cash, "REF-77").ok());

    CHECK(f.count("SELECT count(*) FROM audit_entry WHERE action = 'BILL_GENERATE' AND outcome = 'OK' "
                  "AND record_id = 'BILL-000001' AND user_id = 'STAFF-R1'") == 1);
    CHECK(f.count("SELECT count(*) FROM audit_entry WHERE action = 'PAYMENT_RECORD' AND outcome = 'OK' "
                  "AND record_id = 'RCPT-000001'") == 1);
    for (const char* secret : {"Ravi", "Kumar", "Lipid", "Dressing", "REF-77", "940", "1440"}) {
        CHECK(f.count(std::string("SELECT count(*) FROM audit_entry WHERE ifnull(record_id,'') || ifnull(changed_fields,'') "
                                  "|| ifnull(reason_code,'') LIKE '%") + secret + "%'") == 0);
    }
}

TEST_CASE("TC-Bill-01 a failed save leaves no half-written bill") {
    BillingFixture f;
    const auto doc = f.addDoctor("mehta");
    f.setFee(doc, "500");
    const auto appt = f.completedVisit(f.addPatient("Ravi Kumar"), doc, "mehta", {"CBC"});
    f.db().setFaultHook([](const char* point) {
        if (std::string(point) == "bill_before_commit") throw InjectedFault(point);
    });
    auto failed = f.billing.generateBill(receptionist(), appt.id, {"100"}, {{"Dressing", "1", "40"}});
    f.db().setFaultHook(nullptr);
    REQUIRE_FALSE(failed.ok());
    CHECK(failed.error().kind == ErrorKind::StorageFailure);
    CHECK(f.count("SELECT count(*) FROM bill") == 0);
    CHECK(f.count("SELECT count(*) FROM bill_line") == 0);
    CHECK(f.count("SELECT count(*) FROM audit_entry WHERE action = 'BILL_GENERATE' AND outcome = 'OK'") == 0);
    CHECK(f.billing.generateBill(receptionist(), appt.id, {"100"}, {{"Dressing", "1", "40"}}).ok());  // the retry works
}

TEST_CASE("TC-Bill-02 partial then full payment: the balance is recalculated each time") {
    BillingFixture f;
    const auto doc = f.addDoctor("mehta");
    f.setFee(doc, "1000");
    const auto appt = f.completedVisit(f.addPatient("Ravi Kumar"), doc, "mehta");
    const Bill b = f.billOk(appt.id);

    auto p1 = f.billing.recordPayment(receptionist(), b.billId, "400", PaymentMode::Cash);
    REQUIRE(p1.ok());
    CHECK(p1.value().balanceAfterPaise == 60000);
    CHECK(p1.value().status == BillStatus::PartiallyPaid);
    CHECK(f.billing.getBill(receptionist(), b.billId).value().balancePaise() == 60000);

    auto p2 = f.billing.recordPayment(receptionist(), b.billId, "250.50", PaymentMode::Card, "TXN-9");
    REQUIRE(p2.ok());
    CHECK(p2.value().balanceAfterPaise == 34950);
    CHECK(p2.value().payment.mode == PaymentMode::Card);
    REQUIRE(p2.value().payment.reference.has_value());
    CHECK(*p2.value().payment.reference == "TXN-9");

    auto p3 = f.billing.recordPayment(receptionist(), b.billId, "349.50", PaymentMode::Insurance);
    REQUIRE(p3.ok());
    CHECK(p3.value().balanceAfterPaise == 0);
    CHECK(p3.value().status == BillStatus::Paid);

    auto bill = f.billing.getBill(receptionist(), b.billId);
    REQUIRE(bill.ok());
    CHECK(bill.value().payments.size() == 3);
    CHECK(bill.value().status() == BillStatus::Paid);
    CHECK(f.billing.recordPayment(receptionist(), b.billId, "1", PaymentMode::Cash).error().kind == ErrorKind::Conflict);
}

TEST_CASE("TC-Bill-02 an overpayment, a bad amount or a card number is refused") {
    BillingFixture f;
    const auto doc = f.addDoctor("mehta");
    f.setFee(doc, "500");
    const auto appt = f.completedVisit(f.addPatient("Ravi Kumar"), doc, "mehta");
    const Bill b = f.billOk(appt.id);

    auto over = f.billing.recordPayment(receptionist(), b.billId, "500.01", PaymentMode::Cash);
    REQUIRE_FALSE(over.ok());
    CHECK(over.error().kind == ErrorKind::Conflict);
    for (const char* amount : {"", "0", "-5", "abc", "10.555", "1e3"}) {
        CHECK(f.billing.recordPayment(receptionist(), b.billId, amount, PaymentMode::Cash).error().kind == ErrorKind::Validation);
    }
    CHECK(f.billing.recordPayment(receptionist(), b.billId, "10", PaymentMode::Card, "4111 1111 1111 1111").error().kind ==
          ErrorKind::Validation);
    CHECK(f.billing.recordPayment(receptionist(), b.billId, "10", PaymentMode::Card, "4111-1111-1111-1111").error().kind ==
          ErrorKind::Validation);
    CHECK(f.billing.recordPayment(receptionist(), b.billId, "10", PaymentMode::Cash, "bad;ref").error().kind ==
          ErrorKind::Validation);
    CHECK(f.billing.recordPayment(receptionist(), 999, "10", PaymentMode::Cash).error().kind == ErrorKind::NotFound);
    CHECK(f.billing.recordPayment(doctorSession("mehta"), b.billId, "10", PaymentMode::Cash).error().kind ==
          ErrorKind::AccessDenied);
    CHECK(f.count("SELECT count(*) FROM payment") == 0);
}

TEST_CASE("TC-Bill-02 the store itself refuses a payment above the balance") {
    BillingFixture f;
    const auto doc = f.addDoctor("mehta");
    f.setFee(doc, "500");
    const auto appt = f.completedVisit(f.addPatient("Ravi Kumar"), doc, "mehta");
    const Bill b = f.billOk(appt.id);
    const std::string id = std::to_string(b.billId);
    CHECK_THROWS(f.db().exec("INSERT INTO payment (bill_id, amount_paise, mode, received_by, paid_at) VALUES (" + id +
                             ", 50001, 'CASH', 'x', '2026-10-26T04:00:00Z')"));
    CHECK_NOTHROW(f.db().exec("INSERT INTO payment (bill_id, amount_paise, mode, received_by, paid_at) VALUES (" + id +
                              ", 50000, 'CASH', 'x', '2026-10-26T04:00:00Z')"));
}

TEST_CASE("TC-Bill-02 a failed payment is not recorded") {
    BillingFixture f;
    const auto doc = f.addDoctor("mehta");
    f.setFee(doc, "500");
    const auto appt = f.completedVisit(f.addPatient("Ravi Kumar"), doc, "mehta");
    const Bill b = f.billOk(appt.id);
    f.db().setFaultHook([](const char* point) {
        if (std::string(point) == "payment_before_commit") throw InjectedFault(point);
    });
    auto failed = f.billing.recordPayment(receptionist(), b.billId, "100", PaymentMode::Cash);
    f.db().setFaultHook(nullptr);
    REQUIRE_FALSE(failed.ok());
    CHECK(failed.error().kind == ErrorKind::StorageFailure);
    CHECK(f.count("SELECT count(*) FROM payment") == 0);
    CHECK(f.count("SELECT count(*) FROM audit_entry WHERE action = 'PAYMENT_RECORD' AND outcome = 'OK'") == 0);
    CHECK(f.billing.recordPayment(receptionist(), b.billId, "100", PaymentMode::Cash).ok());
}

TEST_CASE("TC-Bill-02 bills of a patient and bills with a balance are listed") {
    BillingFixture f;
    const auto doc = f.addDoctor("mehta");
    f.setFee(doc, "500");
    const auto ravi = f.addPatient("Ravi Kumar");
    const auto anita = f.addPatient("Anita Rao");
    const Bill a = f.billOk(f.completedVisit(ravi, doc, "mehta").id);
    const Bill b = f.billOk(f.completedVisit(anita, doc, "mehta", {}, at(kMonday, 10, 0)).id);
    REQUIRE(f.billing.recordPayment(receptionist(), a.billId, "500", PaymentMode::Cash).ok());

    auto open = f.billing.listOutstandingBills(receptionist());
    REQUIRE(open.ok());
    REQUIRE(open.value().size() == 1);
    CHECK(open.value()[0].billId == b.billId);
    CHECK(open.value()[0].balancePaise() == 50000);

    auto mine = f.billing.listBillsForPatient(receptionist(), ravi);
    REQUIRE(mine.ok());
    REQUIRE(mine.value().size() == 1);
    CHECK(mine.value()[0].balancePaise() == 0);
    CHECK(f.billing.listBillsForPatient(receptionist(), 999).error().kind == ErrorKind::NotFound);
    CHECK(f.billing.findBillForAppointment(receptionist(), 999).error().kind == ErrorKind::NotFound);
}

TEST_CASE("TC-Bill-03 the receipt shows the bill ID, itemised charges, payment mode and time") {
    BillingFixture f;
    const auto doc = f.addDoctor("mehta");
    f.setFee(doc, "500");
    const auto appt = f.completedVisit(f.addPatient("Ravi Kumar"), doc, "mehta", {"CBC"});
    const Bill b = f.billOk(appt.id, {"300"}, {{"Dressing", "2", "50"}});
    auto paid = f.billing.recordPayment(receptionist(), b.billId, "600", PaymentMode::Card, "TXN-1");
    REQUIRE(paid.ok());

    auto r = f.billing.generateReceipt(receptionist(), paid.value().payment.paymentId);
    REQUIRE(r.ok());
    CHECK(r.value().receiptNumber == "RCPT-000001");
    CHECK(r.value().paidAtLocal == "26-10-2026 09:30");  // the clock reads 09:30 hospital time
    CHECK(r.value().billTotalPaise == 90000);
    CHECK(r.value().paidToDatePaise == 60000);
    CHECK(r.value().balanceAfterPaise == 30000);
    CHECK(r.value().lines.size() == 3);

    const std::string text = renderReceiptText(r.value());
    for (const char* part : {"BILL-000001", "RCPT-000001", "Consultation - Dr mehta", "Test - CBC", "Dressing", "Card",
                             "26-10-2026 09:30", "Ravi Kumar", "HMS-000001", "TXN-1", "Rs 900.00", "Rs 600.00", "Rs 300.00"}) {
        CHECK_MESSAGE(text.find(part) != std::string::npos, part);
    }
    CHECK(f.count("SELECT count(*) FROM audit_entry WHERE action = 'RECEIPT_GENERATE' AND outcome = 'OK' "
                  "AND record_id = 'RCPT-000001'") == 1);
    // Printing it again is allowed and audited again.
    REQUIRE(f.billing.generateReceipt(receptionist(), paid.value().payment.paymentId).ok());
    CHECK(f.count("SELECT count(*) FROM audit_entry WHERE action = 'RECEIPT_GENERATE' AND outcome = 'OK'") == 2);
}

TEST_CASE("TC-Bill-03 each receipt shows what had been paid up to its own payment") {
    BillingFixture f;
    const auto doc = f.addDoctor("mehta");
    f.setFee(doc, "1000");
    const Bill b = f.billOk(f.completedVisit(f.addPatient("Ravi Kumar"), doc, "mehta").id);
    const auto p1 = f.billing.recordPayment(receptionist(), b.billId, "300", PaymentMode::Cash).value();
    const auto p2 = f.billing.recordPayment(receptionist(), b.billId, "700", PaymentMode::Cash).value();
    auto r1 = f.billing.generateReceipt(receptionist(), p1.payment.paymentId).value();
    auto r2 = f.billing.generateReceipt(receptionist(), p2.payment.paymentId).value();
    CHECK(r1.balanceAfterPaise == 70000);
    CHECK(r2.balanceAfterPaise == 0);
    CHECK(r2.paidToDatePaise == 100000);
}

TEST_CASE("TC-Bill-03 the receipt is audited without names, amounts or references; other roles and numbers are refused") {
    BillingFixture f;
    const auto doc = f.addDoctor("mehta");
    f.setFee(doc, "500");
    const Bill b = f.billOk(f.completedVisit(f.addPatient("Ravi Kumar"), doc, "mehta").id);
    const auto p = f.billing.recordPayment(receptionist(), b.billId, "500", PaymentMode::Cash, "REF-5").value();
    REQUIRE(f.billing.generateReceipt(receptionist(), p.payment.paymentId).ok());
    CHECK(f.billing.generateReceipt(receptionist(), 999).error().kind == ErrorKind::NotFound);
    CHECK(f.billing.generateReceipt(doctorSession("mehta"), p.payment.paymentId).error().kind == ErrorKind::AccessDenied);
    for (const char* secret : {"Ravi", "REF-5", "500"}) {
        CHECK(f.count(std::string("SELECT count(*) FROM audit_entry WHERE action = 'RECEIPT_GENERATE' AND "
                                  "ifnull(record_id,'') || ifnull(changed_fields,'') LIKE '%") + secret + "%'") == 0);
    }
}

TEST_CASE("TC-Bill-03 stored UTC times are shown in hospital-local time") {
    CHECK(localDateTimeText("2026-10-26T04:00:00Z", 330) == "26-10-2026 09:30");
    CHECK(localDateTimeText("2026-10-26T19:00:00Z", 330) == "27-10-2026 00:30");  // across midnight
    CHECK(localDateTimeText("2026-12-31T20:00:00Z", 330) == "01-01-2027 01:30");  // across the year
    CHECK(localDateTimeText("not a time", 330) == "not a time");
}

TEST_CASE("Bill and receipt numbers print and parse") {
    CHECK(formatBillId(12) == "BILL-000012");
    CHECK(formatReceiptId(1234567) == "RCPT-1234567");
    CHECK(parseBillId("BILL-000012") == 12);
    CHECK(parseBillId(" bill-12 ") == 12);
    CHECK(parseBillId("12") == 12);
    for (const char* bad : {"", "0", "-1", "x", "BILL-", "BILL-1a", "RCPT-12", "1234567890123"}) CHECK_FALSE(parseBillId(bad).has_value());
    CHECK(parseReceiptId("rcpt-7") == 7);
    CHECK_FALSE(parseReceiptId("BILL-7").has_value());
}

TEST_CASE("TC-Bill-02 an ordinary claim or transaction number is accepted as a reference") {
    BillingFixture f;
    const auto doc = f.addDoctor("mehta");
    f.setFee(doc, "500");
    const Bill b = f.billOk(f.completedVisit(f.addPatient("Ravi Kumar"), doc, "mehta").id);
    auto r = f.billing.recordPayment(receptionist(), b.billId, "10", PaymentMode::Insurance, "CLM/2026/10-0042");
    REQUIRE(r.ok());
    CHECK(*r.value().payment.reference == "CLM/2026/10-0042");
    CHECK(f.billing.recordPayment(receptionist(), b.billId, "10", PaymentMode::Cash, "TXN-20261026-0042").ok());
}
