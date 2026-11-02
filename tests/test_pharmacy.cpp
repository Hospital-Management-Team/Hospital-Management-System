// TC-Pharm-01 (stock check, dispensing and the low-stock alert, HMS-F-041) for
// issue #8. Integration tests against a real SQLite store.
#include <doctest.h>

#include <string>

#include "lab_pharmacy_test_support.hpp"

using namespace hms;
using test::at;
using test::doctorSession;
using test::kMonday;
using test::kTuesday;
using test::LabPharmacyFixture;
using test::labTech;
using test::pharmacistSession;
using test::receptionist;

namespace {
// A patient seen by Dr Mehta with the given medicines; returns the recorded consultation.
ConsultationRecord prescribed(LabPharmacyFixture& f, const std::vector<PrescriptionDraft>& meds) {
    const auto doc = f.addDoctor("mehta");
    return f.seen(f.addPatient("Ravi Kumar"), doc, "mehta", {}, meds);
}
}  // namespace

TEST_CASE("TC-Pharm-01 the pharmacist adds medicines and checks the stock") {
    LabPharmacyFixture f;
    const auto para = f.addMedicine("Paracetamol 500 mg", "tablet", "200");
    const auto amox = f.addMedicine("Amoxicillin 250 mg", "capsule", "");  // no opening stock
    CHECK(formatMedicineId(para.medicineId) == "MED-000001");
    CHECK(para.stockQuantity == 200);
    CHECK(amox.stockQuantity == 0);
    CHECK(amox.lowStock);

    auto stock = f.pharmacy.listStock(pharmacistSession());
    REQUIRE(stock.ok());
    REQUIRE(stock.value().size() == 2);
    CHECK(stock.value()[0].name == "Amoxicillin 250 mg");  // by name
    CHECK(stock.value()[1].stockQuantity == 200);
    CHECK_FALSE(stock.value()[1].lowStock);

    // The same name again, in other letters, is refused.
    auto dup = f.pharmacy.addMedicine(pharmacistSession(), MedicineDraft{"  paracetamol   500 MG ", "tablet", "5"});
    REQUIRE_FALSE(dup.ok());
    CHECK(dup.error().kind == ErrorKind::Conflict);
    CHECK(f.count("SELECT count(*) FROM medicine") == 2);

    // Invalid input is refused with the field named, and nothing is saved.
    auto bad = f.pharmacy.addMedicine(pharmacistSession(), MedicineDraft{"", "", "abc"});
    REQUIRE_FALSE(bad.ok());
    CHECK(bad.error().kind == ErrorKind::Validation);
    CHECK(bad.error().fields.size() == 3);
    CHECK_FALSE(f.pharmacy.addMedicine(pharmacistSession(), MedicineDraft{"Zinc", "tablet", "-5"}).ok());
    CHECK_FALSE(f.pharmacy.addMedicine(pharmacistSession(), MedicineDraft{std::string(101, 'a'), "tablet", "1"}).ok());
    CHECK_FALSE(f.pharmacy.addMedicine(pharmacistSession(), MedicineDraft{"Zinc", std::string(21, 'u'), "1"}).ok());
    CHECK_FALSE(f.pharmacy.addMedicine(pharmacistSession(), MedicineDraft{"Zinc", "tablet", "1000001"}).ok());
    CHECK(f.count("SELECT count(*) FROM medicine") == 2);
}

TEST_CASE("TC-Pharm-01 dispensing reduces the stock by exactly the dispensed quantity") {
    LabPharmacyFixture f;
    const auto c = prescribed(f, {{"Paracetamol", "1 tablet three times a day", "15"}});
    const auto para = f.addMedicine("Paracetamol 500 mg", "tablet", "100");
    const auto line = c.prescriptions[0].itemId;

    auto pending = f.pharmacy.listPendingPrescriptions(pharmacistSession());
    REQUIRE(pending.ok());
    REQUIRE(pending.value().size() == 1);
    CHECK(pending.value()[0].medicineName == "Paracetamol");
    CHECK(pending.value()[0].patientName == "Ravi Kumar");
    CHECK(pending.value()[0].remaining() == 15);

    auto done = f.pharmacy.dispense(pharmacistSession(), line, para.medicineId, "15");
    REQUIRE(done.ok());
    CHECK(done.value().quantity == 15);
    CHECK(done.value().stockAfter == 85);
    CHECK(done.value().remainingToDispense == 0);
    CHECK_FALSE(done.value().lowStock);
    CHECK(f.stockOf(para.medicineId) == 85);
    // Stock is the sum of its movements: 100 opening, 15 out.
    CHECK(f.count("SELECT sum(delta) FROM stock_movement WHERE medicine_id = 1") == 85);
    CHECK(f.count("SELECT count(*) FROM dispense WHERE item_id = " + std::to_string(line) + " AND quantity = 15 "
                  "AND dispensed_by = 'pharm1'") == 1);

    // The finished line is no longer waiting.
    auto left = f.pharmacy.listPendingPrescriptions(pharmacistSession());
    REQUIRE(left.ok());
    CHECK(left.value().empty());
    auto all = f.pharmacy.listPrescriptionsForPatient(pharmacistSession(), c.uhid);
    REQUIRE(all.ok());
    REQUIRE(all.value().size() == 1);
    CHECK(all.value()[0].dispensed == 15);
}

TEST_CASE("TC-Pharm-01 a prescription line can be dispensed in parts but never beyond the prescribed quantity") {
    LabPharmacyFixture f;
    const auto c = prescribed(f, {{"Paracetamol", "twice a day", "10"}});
    const auto para = f.addMedicine("Paracetamol 500 mg", "tablet", "100");
    const auto line = c.prescriptions[0].itemId;

    auto first = f.pharmacy.dispense(pharmacistSession(), line, para.medicineId, "4");
    REQUIRE(first.ok());
    CHECK(first.value().remainingToDispense == 6);
    CHECK(f.pharmacy.listPendingPrescriptions(pharmacistSession()).value().size() == 1);  // still waiting

    auto tooMany = f.pharmacy.dispense(pharmacistSession(), line, para.medicineId, "7");
    REQUIRE_FALSE(tooMany.ok());
    CHECK(tooMany.error().kind == ErrorKind::Conflict);
    CHECK(f.stockOf(para.medicineId) == 96);  // unchanged by the refusal

    auto rest = f.pharmacy.dispense(pharmacistSession(), line, para.medicineId, "6");
    REQUIRE(rest.ok());
    CHECK(rest.value().remainingToDispense == 0);
    CHECK(f.stockOf(para.medicineId) == 90);

    auto again = f.pharmacy.dispense(pharmacistSession(), line, para.medicineId, "1");
    REQUIRE_FALSE(again.ok());
    CHECK(again.error().kind == ErrorKind::Conflict);
    CHECK(f.stockOf(para.medicineId) == 90);
    CHECK(f.count("SELECT count(*) FROM dispense") == 2);
}

TEST_CASE("TC-Pharm-01 dispensing more than the stock is refused and nothing changes") {
    LabPharmacyFixture f;
    const auto c = prescribed(f, {{"Paracetamol", "twice a day", "30"}});
    const auto para = f.addMedicine("Paracetamol 500 mg", "tablet", "20");

    auto r = f.pharmacy.dispense(pharmacistSession(), c.prescriptions[0].itemId, para.medicineId, "30");
    REQUIRE_FALSE(r.ok());
    CHECK(r.error().kind == ErrorKind::Conflict);
    CHECK(r.error().message.find("Only 20 tablet") != std::string::npos);
    CHECK(f.stockOf(para.medicineId) == 20);
    CHECK(f.count("SELECT count(*) FROM dispense") == 0);
    CHECK(f.count("SELECT count(*) FROM stock_movement WHERE reason = 'DISPENSE'") == 0);
    CHECK(f.count("SELECT count(*) FROM audit_entry WHERE action = 'MEDICINE_DISPENSE' AND outcome = 'OK'") == 0);

    // What is in stock can be given now and the rest after a restock.
    REQUIRE(f.pharmacy.dispense(pharmacistSession(), c.prescriptions[0].itemId, para.medicineId, "20").ok());
    CHECK(f.stockOf(para.medicineId) == 0);
    REQUIRE(f.pharmacy.restock(pharmacistSession(), para.medicineId, "50").ok());
    REQUIRE(f.pharmacy.dispense(pharmacistSession(), c.prescriptions[0].itemId, para.medicineId, "10").ok());
    CHECK(f.stockOf(para.medicineId) == 40);
}

TEST_CASE("TC-Pharm-01 invalid dispensing input is refused") {
    LabPharmacyFixture f;
    const auto c = prescribed(f, {{"Paracetamol", "twice a day", "10"}});
    const auto para = f.addMedicine("Paracetamol 500 mg", "tablet", "50");
    const auto line = c.prescriptions[0].itemId;

    for (const char* q : {"", "0", "-1", "abc", "1.5", "2 3"}) {
        auto r = f.pharmacy.dispense(pharmacistSession(), line, para.medicineId, q);
        REQUIRE_FALSE(r.ok());
        CHECK(r.error().kind == ErrorKind::Validation);
    }
    auto noLine = f.pharmacy.dispense(pharmacistSession(), 99, para.medicineId, "1");
    REQUIRE_FALSE(noLine.ok());
    CHECK(noLine.error().kind == ErrorKind::NotFound);
    auto noMedicine = f.pharmacy.dispense(pharmacistSession(), line, 99, "1");
    REQUIRE_FALSE(noMedicine.ok());
    CHECK(noMedicine.error().kind == ErrorKind::NotFound);
    CHECK(f.stockOf(para.medicineId) == 50);
    CHECK(f.count("SELECT count(*) FROM dispense") == 0);
    CHECK(parsePrescriptionId("RX-000045") == 45);
    CHECK(parseMedicineId("med-3") == 3);
    CHECK_FALSE(parseMedicineId("RX-3").has_value());
    CHECK(formatPrescriptionId(45) == "RX-000045");
}

TEST_CASE("TC-Pharm-01 the low-stock alert fires when the stock falls BELOW the threshold (E-08)") {
    LabPharmacyFixture f;
    const auto c = prescribed(f, {{"Paracetamol", "daily", "20"}});
    const auto para = f.addMedicine("Paracetamol 500 mg", "tablet", "12");  // the threshold is 10
    const auto line = c.prescriptions[0].itemId;
    CHECK(f.policy.labPharmacy.lowStockThreshold == 10);

    auto a = f.pharmacy.dispense(pharmacistSession(), line, para.medicineId, "1");  // 11 left
    REQUIRE(a.ok());
    CHECK_FALSE(a.value().lowStock);
    auto b = f.pharmacy.dispense(pharmacistSession(), line, para.medicineId, "1");  // exactly 10 left
    REQUIRE(b.ok());
    CHECK(b.value().stockAfter == 10);
    CHECK_FALSE(b.value().lowStock);  // "at" the threshold is not "below" it
    auto low = f.pharmacy.dispense(pharmacistSession(), line, para.medicineId, "1");  // 9 left
    REQUIRE(low.ok());
    CHECK(low.value().stockAfter == 9);
    CHECK(low.value().lowStock);
    CHECK(low.value().threshold == 10);

    auto stock = f.pharmacy.listStock(pharmacistSession());
    REQUIRE(stock.ok());
    CHECK(stock.value()[0].lowStock);

    // A restock above the threshold clears the alert.
    auto restocked = f.pharmacy.restock(pharmacistSession(), para.medicineId, "5");
    REQUIRE(restocked.ok());
    CHECK(restocked.value().stockQuantity == 14);
    CHECK_FALSE(restocked.value().lowStock);

    // The threshold comes from the policy, so a different value changes the alert.
    AppPolicy strict = f.policy;
    strict.labPharmacy.lowStockThreshold = 20;
    PharmacyService other(f.db(), f.clock, strict);
    CHECK(other.listStock(pharmacistSession()).value()[0].lowStock);
}

TEST_CASE("TC-Pharm-01 restocking adds to the stock; invalid or excessive quantities are refused") {
    LabPharmacyFixture f;
    const auto para = f.addMedicine("Paracetamol 500 mg", "tablet", "5");

    auto r = f.pharmacy.restock(pharmacistSession(), para.medicineId, "95");
    REQUIRE(r.ok());
    CHECK(r.value().stockQuantity == 100);
    CHECK(f.count("SELECT count(*) FROM stock_movement WHERE reason = 'RESTOCK' AND delta = 95") == 1);
    CHECK(f.count("SELECT count(*) FROM audit_entry WHERE action = 'STOCK_RESTOCK' AND outcome = 'OK' "
                  "AND record_id = 'MED-000001'") == 1);

    for (const char* q : {"", "0", "-3", "abc", "1000000"}) {  // the last would take the stock above the limit
        auto bad = f.pharmacy.restock(pharmacistSession(), para.medicineId, q);
        REQUIRE_FALSE(bad.ok());
        CHECK(bad.error().kind == ErrorKind::Validation);
    }
    CHECK(f.pharmacy.restock(pharmacistSession(), 99, "5").error().kind == ErrorKind::NotFound);
    CHECK(f.stockOf(para.medicineId) == 100);
}

TEST_CASE("TC-Pharm-01 only the Pharmacist manages and dispenses; denials are audited") {
    LabPharmacyFixture f;
    const auto c = prescribed(f, {{"Paracetamol", "daily", "5"}});
    const auto para = f.addMedicine("Paracetamol 500 mg", "tablet", "50");

    for (const Session& s : {receptionist(), doctorSession("mehta"), labTech()}) {
        CHECK(f.pharmacy.listStock(s).error().kind == ErrorKind::AccessDenied);
        CHECK(f.pharmacy.addMedicine(s, MedicineDraft{"Zinc", "tablet", "5"}).error().kind == ErrorKind::AccessDenied);
        CHECK(f.pharmacy.restock(s, para.medicineId, "5").error().kind == ErrorKind::AccessDenied);
        CHECK(f.pharmacy.listPendingPrescriptions(s).error().kind == ErrorKind::AccessDenied);
        CHECK(f.pharmacy.listPrescriptionsForPatient(s, c.uhid).error().kind == ErrorKind::AccessDenied);
        CHECK(f.pharmacy.dispense(s, c.prescriptions[0].itemId, para.medicineId, "1").error().kind == ErrorKind::AccessDenied);
    }
    CHECK(f.stockOf(para.medicineId) == 50);
    CHECK(f.count("SELECT count(*) FROM medicine") == 1);
    CHECK(f.count("SELECT count(*) FROM dispense") == 0);
    CHECK(f.count("SELECT count(*) FROM audit_entry WHERE action = 'MEDICINE_DISPENSE' AND outcome = 'DENIED'") == 3);
    CHECK(f.count("SELECT count(*) FROM audit_entry WHERE action = 'STOCK_VIEW' AND outcome = 'DENIED'") == 3);
    CHECK(f.count("SELECT count(*) FROM audit_entry WHERE action = 'STOCK_RESTOCK' AND outcome = 'DENIED'") == 3);
    CHECK(f.count("SELECT count(*) FROM audit_entry WHERE action = 'MEDICINE_ADD' AND outcome = 'DENIED'") == 3);
    CHECK(f.count("SELECT count(*) FROM audit_entry WHERE action = 'PRESCRIPTION_VIEW' AND outcome = 'DENIED'") == 6);
}

TEST_CASE("TC-Pharm-01 the audit entries hold IDs and field names only") {
    LabPharmacyFixture f;
    const auto c = prescribed(f, {{"Cetirizine", "at night", "7"}});
    const auto med = f.addMedicine("Cetirizine 10 mg", "tablet", "30");
    REQUIRE(f.pharmacy.dispense(pharmacistSession(), c.prescriptions[0].itemId, med.medicineId, "7").ok());

    CHECK(f.count("SELECT count(*) FROM audit_entry WHERE action = 'MEDICINE_ADD' AND outcome = 'OK' "
                  "AND record_id = 'MED-000001' AND user_id = 'pharm1'") == 1);
    CHECK(f.count("SELECT count(*) FROM audit_entry WHERE action = 'MEDICINE_DISPENSE' AND outcome = 'OK' "
                  "AND record_id = 'RX-000001'") == 1);
    for (const char* secret : {"Ravi", "Kumar", "Cetirizine", "tablet", "at night"}) {
        CHECK(f.count(std::string("SELECT count(*) FROM audit_entry WHERE ifnull(record_id,'') || ifnull(changed_fields,'') "
                                  "|| ifnull(reason_code,'') LIKE '%") + secret + "%'") == 0);
    }
}

TEST_CASE("TC-Pharm-01 the store itself refuses over-dispensing and negative stock") {
    LabPharmacyFixture f;
    const auto c = prescribed(f, {{"Paracetamol", "daily", "5"}});
    const auto para = f.addMedicine("Paracetamol 500 mg", "tablet", "50");
    const std::string line = std::to_string(c.prescriptions[0].itemId);

    CHECK_THROWS_AS(f.db().exec("INSERT INTO dispense (item_id, medicine_id, quantity, dispensed_by, dispensed_at) "
                                "VALUES (" + line + ", 1, 6, 'x', '2026-10-26T04:00:00Z')"), DbError);
    CHECK_THROWS_AS(f.db().exec("UPDATE medicine SET stock_quantity = -1 WHERE medicine_id = 1"), DbError);
    CHECK_THROWS_AS(f.db().exec("INSERT INTO stock_movement (medicine_id, delta, reason, recorded_by, recorded_at) "
                                "VALUES (1, -1, 'DISPENSE', 'x', 'now')"), DbError);  // a dispense row needs its dispense
    CHECK(f.stockOf(para.medicineId) == 50);
    CHECK(f.count("SELECT count(*) FROM dispense") == 0);
}

TEST_CASE("TC-Pharm-01 NF-002 a failure before commit saves nothing and the retry works") {
    LabPharmacyFixture f;
    const auto c = prescribed(f, {{"Paracetamol", "daily", "5"}});
    const auto para = f.addMedicine("Paracetamol 500 mg", "tablet", "50");

    f.db().setFaultHook([](const char* point) {
        if (std::string(point) == "dispense_before_commit") throw InjectedFault(point);
    });
    auto failed = f.pharmacy.dispense(pharmacistSession(), c.prescriptions[0].itemId, para.medicineId, "5");
    f.db().setFaultHook(nullptr);
    REQUIRE_FALSE(failed.ok());
    CHECK(failed.error().kind == ErrorKind::StorageFailure);
    CHECK(f.stockOf(para.medicineId) == 50);
    CHECK(f.count("SELECT count(*) FROM dispense") == 0);
    CHECK(f.count("SELECT count(*) FROM stock_movement WHERE reason = 'DISPENSE'") == 0);
    CHECK(f.count("SELECT count(*) FROM audit_entry WHERE action = 'MEDICINE_DISPENSE' AND outcome = 'OK'") == 0);
    CHECK(f.pharmacy.dispense(pharmacistSession(), c.prescriptions[0].itemId, para.medicineId, "5").ok());
    CHECK(f.stockOf(para.medicineId) == 45);

    f.db().setFaultHook([](const char* point) {
        if (std::string(point) == "restock_before_commit") throw InjectedFault(point);
    });
    CHECK_FALSE(f.pharmacy.restock(pharmacistSession(), para.medicineId, "10").ok());
    f.db().setFaultHook(nullptr);
    CHECK(f.stockOf(para.medicineId) == 45);
}

TEST_CASE("TC-Pharm-01 prescriptions of a patient are listed newest first; an unknown patient is not found") {
    LabPharmacyFixture f;
    const auto doc = f.addDoctor("mehta");
    const auto uhid = f.addPatient("Ravi Kumar");
    f.seen(uhid, doc, "mehta", {}, {{"Paracetamol", "daily", "5"}});
    f.seen(uhid, doc, "mehta", {}, {{"Cetirizine", "at night", "7"}, {"Vitamin C", "daily", "10"}}, at(kTuesday, 9, 0));

    auto rows = f.pharmacy.listPrescriptionsForPatient(pharmacistSession(), uhid);
    REQUIRE(rows.ok());
    REQUIRE(rows.value().size() == 3);
    CHECK(rows.value()[0].medicineName == "Vitamin C");
    CHECK(rows.value()[2].medicineName == "Paracetamol");
    auto none = f.pharmacy.listPrescriptionsForPatient(pharmacistSession(), 999);
    REQUIRE_FALSE(none.ok());
    CHECK(none.error().kind == ErrorKind::NotFound);
}

TEST_CASE("TC-Pharm-01 a wrongly typed name or unit can be corrected; a clash, a no-op or bad input is refused") {
    LabPharmacyFixture f;
    const auto para = f.addMedicine("Paracetmol 500 mg", "tablet", "40");  // typed wrongly
    const auto amox = f.addMedicine("Amoxicillin 250 mg", "capsule", "40");

    auto fixed = f.pharmacy.updateMedicine(pharmacistSession(), para.medicineId, MedicineEdit{"  Paracetamol   500 mg ", "strip"});
    REQUIRE(fixed.ok());
    CHECK(fixed.value().name == "Paracetamol 500 mg");
    CHECK(fixed.value().unit == "strip");
    CHECK(f.stockOf(para.medicineId) == 40);  // the stock is untouched
    CHECK(f.count("SELECT count(*) FROM audit_entry WHERE action = 'MEDICINE_UPDATE' AND outcome = 'OK' "
                  "AND record_id = 'MED-000001'") == 1);

    auto same = f.pharmacy.updateMedicine(pharmacistSession(), para.medicineId, MedicineEdit{"Paracetamol 500 mg", "strip"});
    REQUIRE_FALSE(same.ok());
    CHECK(same.error().kind == ErrorKind::Conflict);
    auto clash = f.pharmacy.updateMedicine(pharmacistSession(), para.medicineId, MedicineEdit{"AMOXICILLIN 250 MG", "strip"});
    REQUIRE_FALSE(clash.ok());
    CHECK(clash.error().kind == ErrorKind::Conflict);
    auto bad = f.pharmacy.updateMedicine(pharmacistSession(), amox.medicineId, MedicineEdit{"", ""});
    REQUIRE_FALSE(bad.ok());
    CHECK(bad.error().kind == ErrorKind::Validation);
    CHECK(bad.error().fields.size() == 2);
    CHECK(f.pharmacy.updateMedicine(pharmacistSession(), 99, MedicineEdit{"Zinc", "tablet"}).error().kind == ErrorKind::NotFound);
    // Changing only the letter case of its own name is allowed (it is not a clash with itself).
    CHECK(f.pharmacy.updateMedicine(pharmacistSession(), amox.medicineId, MedicineEdit{"AMOXICILLIN 250 mg", "capsule"}).ok());
}

TEST_CASE("TC-Pharm-01 a stock count is corrected to the counted figure and the reason is kept") {
    LabPharmacyFixture f;
    const auto para = f.addMedicine("Paracetamol 500 mg", "tablet", "50");

    auto down = f.pharmacy.correctStock(pharmacistSession(), para.medicineId, "44", "six damaged strips");
    REQUIRE(down.ok());
    CHECK(down.value().stockQuantity == 44);
    auto up = f.pharmacy.correctStock(pharmacistSession(), para.medicineId, "46", "found a box in the store room");
    REQUIRE(up.ok());
    CHECK(up.value().stockQuantity == 46);
    CHECK(f.stockOf(para.medicineId) == 46);
    // Stock is still the sum of its movements: 50 opening, -6 and +2 corrections.
    CHECK(f.count("SELECT sum(delta) FROM stock_movement WHERE medicine_id = 1") == 46);
    CHECK(f.count("SELECT count(*) FROM stock_movement WHERE reason = 'CORRECTION' AND delta = -6 "
                  "AND note = 'six damaged strips' AND recorded_by = 'pharm1'") == 1);
    CHECK(f.count("SELECT count(*) FROM stock_movement WHERE reason = 'CORRECTION' AND delta = 2") == 1);

    // Counted to zero is allowed; the same figure again, a missing reason or a bad number is not.
    auto same = f.pharmacy.correctStock(pharmacistSession(), para.medicineId, "46", "recount");
    REQUIRE_FALSE(same.ok());
    CHECK(same.error().kind == ErrorKind::Conflict);
    for (const char* q : {"", "-1", "abc", "1.5", "1000001"}) {
        auto r = f.pharmacy.correctStock(pharmacistSession(), para.medicineId, q, "recount");
        REQUIRE_FALSE(r.ok());
        CHECK(r.error().kind == ErrorKind::Validation);
    }
    auto noReason = f.pharmacy.correctStock(pharmacistSession(), para.medicineId, "40", "   ");
    REQUIRE_FALSE(noReason.ok());
    CHECK(noReason.error().kind == ErrorKind::Validation);
    CHECK(f.pharmacy.correctStock(pharmacistSession(), para.medicineId, "40", std::string(101, 'r')).error().kind ==
          ErrorKind::Validation);
    CHECK(f.pharmacy.correctStock(pharmacistSession(), 99, "5", "recount").error().kind == ErrorKind::NotFound);
    CHECK(f.stockOf(para.medicineId) == 46);

    auto zero = f.pharmacy.correctStock(pharmacistSession(), para.medicineId, "0", "expired, discarded");
    REQUIRE(zero.ok());
    CHECK(zero.value().stockQuantity == 0);
    CHECK(zero.value().lowStock);

    // The audit entry holds the ID and field name, never the reason.
    CHECK(f.count("SELECT count(*) FROM audit_entry WHERE action = 'STOCK_CORRECT' AND outcome = 'OK' "
                  "AND record_id = 'MED-000001' AND changed_fields LIKE '%stock%'") == 3);
    for (const char* secret : {"damaged", "store room", "expired"}) {
        CHECK(f.count(std::string("SELECT count(*) FROM audit_entry WHERE ifnull(record_id,'') || ifnull(changed_fields,'') "
                                  "|| ifnull(reason_code,'') LIKE '%") + secret + "%'") == 0);
    }
}

TEST_CASE("TC-Pharm-01 an unavailable medicine cannot be dispensed or restocked, raises no alert, and can return") {
    LabPharmacyFixture f;
    const auto c = prescribed(f, {{"Paracetamol", "daily", "5"}});
    const auto para = f.addMedicine("Paracetamol 500 mg", "tablet", "4");  // already below the threshold
    CHECK(f.pharmacy.listStock(pharmacistSession()).value()[0].lowStock);

    auto off = f.pharmacy.setMedicineActive(pharmacistSession(), para.medicineId, false);
    REQUIRE(off.ok());
    CHECK_FALSE(off.value().active);
    CHECK_FALSE(off.value().lowStock);  // no alert for a medicine that is no longer used
    CHECK_FALSE(f.pharmacy.setMedicineActive(pharmacistSession(), para.medicineId, false).ok());  // already off

    auto dispense = f.pharmacy.dispense(pharmacistSession(), c.prescriptions[0].itemId, para.medicineId, "1");
    REQUIRE_FALSE(dispense.ok());
    CHECK(dispense.error().kind == ErrorKind::Conflict);
    auto restock = f.pharmacy.restock(pharmacistSession(), para.medicineId, "10");
    REQUIRE_FALSE(restock.ok());
    CHECK(restock.error().kind == ErrorKind::Conflict);
    CHECK(f.stockOf(para.medicineId) == 4);
    CHECK(f.count("SELECT count(*) FROM dispense") == 0);
    CHECK(f.pharmacy.listStock(pharmacistSession()).value().size() == 1);  // still listed, with its history

    auto on = f.pharmacy.setMedicineActive(pharmacistSession(), para.medicineId, true);
    REQUIRE(on.ok());
    CHECK(on.value().active);
    CHECK(on.value().lowStock);
    CHECK(f.pharmacy.dispense(pharmacistSession(), c.prescriptions[0].itemId, para.medicineId, "1").ok());
    CHECK(f.pharmacy.setMedicineActive(pharmacistSession(), 99, false).error().kind == ErrorKind::NotFound);
    CHECK(f.count("SELECT count(*) FROM audit_entry WHERE action = 'MEDICINE_UPDATE' AND outcome = 'OK'") == 2);
}

TEST_CASE("TC-Pharm-01 only the Pharmacist edits medicines or corrects stock; denials are audited") {
    LabPharmacyFixture f;
    const auto para = f.addMedicine("Paracetamol 500 mg", "tablet", "50");

    for (const Session& s : {receptionist(), doctorSession("mehta"), labTech()}) {
        CHECK(f.pharmacy.updateMedicine(s, para.medicineId, MedicineEdit{"Zinc", "tablet"}).error().kind == ErrorKind::AccessDenied);
        CHECK(f.pharmacy.setMedicineActive(s, para.medicineId, false).error().kind == ErrorKind::AccessDenied);
        CHECK(f.pharmacy.correctStock(s, para.medicineId, "1", "recount").error().kind == ErrorKind::AccessDenied);
    }
    CHECK(f.stockOf(para.medicineId) == 50);
    CHECK(f.count("SELECT count(*) FROM medicine WHERE name = 'Paracetamol 500 mg' AND active = 1") == 1);
    CHECK(f.count("SELECT count(*) FROM audit_entry WHERE action = 'MEDICINE_UPDATE' AND outcome = 'DENIED'") == 6);
    CHECK(f.count("SELECT count(*) FROM audit_entry WHERE action = 'STOCK_CORRECT' AND outcome = 'DENIED'") == 3);
}

TEST_CASE("TC-Pharm-01 the store refuses a correction without a reason and a dispense movement without its dispense") {
    LabPharmacyFixture f;
    f.addMedicine("Paracetamol 500 mg", "tablet", "50");
    CHECK_THROWS_AS(f.db().exec("INSERT INTO stock_movement (medicine_id, delta, reason, recorded_by, recorded_at) "
                                "VALUES (1, -2, 'CORRECTION', 'x', 'now')"), DbError);
    CHECK_THROWS_AS(f.db().exec("INSERT INTO stock_movement (medicine_id, delta, reason, note, recorded_by, recorded_at) "
                                "VALUES (1, 2, 'RESTOCK', 'a note does not belong here', 'x', 'now')"), DbError);
    CHECK(f.count("SELECT count(*) FROM stock_movement") == 1);
}

TEST_CASE("TC-Pharm-01 NF-002 a failed correction saves nothing") {
    LabPharmacyFixture f;
    const auto para = f.addMedicine("Paracetamol 500 mg", "tablet", "50");
    f.db().setFaultHook([](const char* point) {
        if (std::string(point) == "stock_correct_before_commit" || std::string(point) == "medicine_update_before_commit")
            throw InjectedFault(point);
    });
    CHECK_FALSE(f.pharmacy.correctStock(pharmacistSession(), para.medicineId, "40", "recount").ok());
    CHECK_FALSE(f.pharmacy.updateMedicine(pharmacistSession(), para.medicineId, MedicineEdit{"Zinc", "tablet"}).ok());
    CHECK_FALSE(f.pharmacy.setMedicineActive(pharmacistSession(), para.medicineId, false).ok());
    f.db().setFaultHook(nullptr);
    CHECK(f.stockOf(para.medicineId) == 50);
    CHECK(f.count("SELECT count(*) FROM stock_movement WHERE reason = 'CORRECTION'") == 0);
    CHECK(f.count("SELECT count(*) FROM medicine WHERE name = 'Paracetamol 500 mg' AND active = 1") == 1);
}
