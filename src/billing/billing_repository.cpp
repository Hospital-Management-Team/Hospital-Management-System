#include "billing/billing_repository.hpp"

namespace hms {

namespace {
const char* kBillSelect =
    "SELECT b.bill_id, b.uhid, p.full_name, b.appointment_id, a.slot_start, d.username, b.total_paise, "
    "b.created_by, b.created_at, COALESCE((SELECT SUM(y.amount_paise) FROM payment y WHERE y.bill_id = b.bill_id), 0) "
    "FROM bill b "
    "JOIN patient p ON p.uhid = b.uhid "
    "JOIN appointment a ON a.appointment_id = b.appointment_id "
    "JOIN staff_accounts d ON d.staff_id = a.doctor_staff_id ";

PaymentRecord readPayment(const Statement& st) {
    PaymentRecord r;
    r.paymentId = st.getInt64(0);
    r.billId = st.getInt64(1);
    r.amountPaise = st.getInt64(2);
    r.mode = paymentModeFromStorage(st.getText(3)).value_or(PaymentMode::Cash);
    r.reference = st.getOptionalText(4);
    r.receivedBy = st.getText(5);
    r.paidAt = st.getText(6);
    return r;
}

BillSummary readSummary(const Statement& st) {
    BillSummary s;
    s.billId = st.getInt64(0);
    s.uhid = st.getInt64(1);
    s.patientName = st.getText(2);
    s.totalPaise = st.getInt64(3);
    s.createdAt = st.getText(4);
    s.paidPaise = st.getInt64(5);
    return s;
}

const char* kSummarySelect =
    "SELECT b.bill_id, b.uhid, p.full_name, b.total_paise, b.created_at, "
    "COALESCE((SELECT SUM(y.amount_paise) FROM payment y WHERE y.bill_id = b.bill_id), 0) "
    "FROM bill b JOIN patient p ON p.uhid = b.uhid ";
}  // namespace

std::optional<std::int64_t> BillingRepository::consultationIdForAppointment(std::int64_t appointmentId) {
    Statement st = db_.prepare("SELECT consultation_id FROM consultation WHERE appointment_id = ?1");
    st.bind(1, appointmentId);
    if (!st.step()) return std::nullopt;
    return st.getInt64(0);
}

std::vector<BillableTest> BillingRepository::testsForConsultation(std::int64_t consultationId) {
    Statement st = db_.prepare(
        "SELECT order_id, test_name FROM lab_order WHERE consultation_id = ?1 ORDER BY order_id");
    st.bind(1, consultationId);
    std::vector<BillableTest> out;
    while (st.step()) out.push_back(BillableTest{st.getInt64(0), st.getText(1)});
    return out;
}

std::optional<std::string> BillingRepository::doctorUsername(std::int64_t staffId) {
    Statement st = db_.prepare("SELECT username FROM staff_accounts WHERE staff_id = ?1");
    st.bind(1, staffId);
    if (!st.step()) return std::nullopt;
    return st.getText(0);
}

bool BillingRepository::patientExists(std::int64_t uhid) {
    Statement st = db_.prepare("SELECT 1 FROM patient WHERE uhid = ?1");
    st.bind(1, uhid);
    return st.step();
}

bool BillingRepository::hasDoctorProfile(std::int64_t staffId) {
    Statement st = db_.prepare("SELECT 1 FROM doctor_profile WHERE staff_id = ?1");
    st.bind(1, staffId);
    return st.step();
}

void BillingRepository::saveFee(std::int64_t staffId, std::int64_t feePaise, const std::string& nowUtc) {
    Statement st = db_.prepare(
        "INSERT INTO doctor_fee (staff_id, fee_paise, updated_at) VALUES (?1, ?2, ?3) "
        "ON CONFLICT (staff_id) DO UPDATE SET fee_paise = excluded.fee_paise, updated_at = excluded.updated_at");
    st.bind(1, staffId).bind(2, feePaise).bind(3, nowUtc);
    st.run();
}

std::optional<std::int64_t> BillingRepository::feeOf(std::int64_t staffId) {
    Statement st = db_.prepare("SELECT fee_paise FROM doctor_fee WHERE staff_id = ?1");
    st.bind(1, staffId);
    if (!st.step()) return std::nullopt;
    return st.getInt64(0);
}

std::vector<DoctorFee> BillingRepository::listDoctorFees() {
    Statement st = db_.prepare(
        "SELECT d.staff_id, s.username, d.department, f.fee_paise FROM doctor_profile d "
        "JOIN staff_accounts s ON s.staff_id = d.staff_id "
        "LEFT JOIN doctor_fee f ON f.staff_id = d.staff_id ORDER BY s.username");
    std::vector<DoctorFee> out;
    while (st.step()) {
        DoctorFee f;
        f.staffId = st.getInt64(0);
        f.username = st.getText(1);
        f.department = st.getText(2);
        f.feePaise = st.getOptionalInt64(3);
        out.push_back(std::move(f));
    }
    return out;
}

std::int64_t BillingRepository::insertBill(std::int64_t uhid, std::int64_t appointmentId, std::int64_t totalPaise,
                                           const std::string& createdBy, const std::string& nowUtc) {
    Statement st = db_.prepare(
        "INSERT INTO bill (uhid, appointment_id, total_paise, created_by, created_at) VALUES (?1, ?2, ?3, ?4, ?5)");
    st.bind(1, uhid).bind(2, appointmentId).bind(3, totalPaise).bind(4, createdBy).bind(5, nowUtc);
    st.run();
    return db_.lastInsertRowId();
}

void BillingRepository::insertLine(std::int64_t billId, const NewBillLine& line) {
    Statement st = db_.prepare(
        "INSERT INTO bill_line (bill_id, kind, description, quantity, unit_price_paise, amount_paise, lab_order_id) "
        "VALUES (?1, ?2, ?3, ?4, ?5, ?6, ?7)");
    st.bind(1, billId)
        .bind(2, line.kind)
        .bind(3, line.description)
        .bind(4, line.quantity)
        .bind(5, line.unitPricePaise)
        .bind(6, line.amountPaise())
        .bind(7, line.labOrderId);
    st.run();
}

std::optional<Bill> BillingRepository::loadBill(const std::string& where,
                                                const std::function<void(Statement&)>& bind) {
    Bill bill;
    {
        Statement st = db_.prepare(std::string(kBillSelect) + where);
        bind(st);
        if (!st.step()) return std::nullopt;
        bill.billId = st.getInt64(0);
        bill.uhid = st.getInt64(1);
        bill.patientName = st.getText(2);
        bill.appointmentId = st.getInt64(3);
        bill.slotStart = st.getText(4);
        bill.doctorName = st.getText(5);
        bill.totalPaise = st.getInt64(6);
        bill.createdBy = st.getText(7);
        bill.createdAt = st.getText(8);
        bill.paidPaise = st.getInt64(9);
    }
    {
        Statement st = db_.prepare(
            "SELECT line_id, kind, description, quantity, unit_price_paise, amount_paise FROM bill_line "
            "WHERE bill_id = ?1 ORDER BY line_id");
        st.bind(1, bill.billId);
        while (st.step()) {
            bill.lines.push_back(BillLine{st.getInt64(0), st.getText(1), st.getText(2), st.getInt64(3),
                                          st.getInt64(4), st.getInt64(5)});
        }
    }
    Statement st = db_.prepare(
        "SELECT payment_id, bill_id, amount_paise, mode, reference, received_by, paid_at FROM payment "
        "WHERE bill_id = ?1 ORDER BY payment_id");
    st.bind(1, bill.billId);
    while (st.step()) bill.payments.push_back(readPayment(st));
    return bill;
}

std::optional<Bill> BillingRepository::findBill(std::int64_t billId) {
    return loadBill("WHERE b.bill_id = ?1", [&](Statement& st) { st.bind(1, billId); });
}

std::optional<Bill> BillingRepository::findBillByAppointment(std::int64_t appointmentId) {
    return loadBill("WHERE b.appointment_id = ?1", [&](Statement& st) { st.bind(1, appointmentId); });
}

std::vector<BillSummary> BillingRepository::listForPatient(std::int64_t uhid) {
    Statement st = db_.prepare(std::string(kSummarySelect) + "WHERE b.uhid = ?1 ORDER BY b.bill_id DESC");
    st.bind(1, uhid);
    std::vector<BillSummary> out;
    while (st.step()) out.push_back(readSummary(st));
    return out;
}

std::vector<BillSummary> BillingRepository::listOutstanding(std::size_t limit) {
    Statement st = db_.prepare(
        std::string(kSummarySelect) +
        "WHERE b.total_paise > COALESCE((SELECT SUM(y.amount_paise) FROM payment y WHERE y.bill_id = b.bill_id), 0) "
        "ORDER BY b.bill_id LIMIT ?1");
    st.bind(1, static_cast<std::int64_t>(limit));
    std::vector<BillSummary> out;
    while (st.step()) out.push_back(readSummary(st));
    return out;
}

std::int64_t BillingRepository::insertPayment(std::int64_t billId, std::int64_t amountPaise, PaymentMode mode,
                                              const std::optional<std::string>& reference,
                                              const std::string& receivedBy, const std::string& nowUtc) {
    Statement st = db_.prepare(
        "INSERT INTO payment (bill_id, amount_paise, mode, reference, received_by, paid_at) "
        "VALUES (?1, ?2, ?3, ?4, ?5, ?6)");
    st.bind(1, billId).bind(2, amountPaise).bind(3, to_string(mode)).bind(4, reference).bind(5, receivedBy).bind(6, nowUtc);
    st.run();
    return db_.lastInsertRowId();
}

std::optional<PaymentRecord> BillingRepository::findPayment(std::int64_t paymentId) {
    Statement st = db_.prepare(
        "SELECT payment_id, bill_id, amount_paise, mode, reference, received_by, paid_at FROM payment "
        "WHERE payment_id = ?1");
    st.bind(1, paymentId);
    if (!st.step()) return std::nullopt;
    return readPayment(st);
}

std::int64_t BillingRepository::paidUpTo(std::int64_t billId, std::int64_t paymentId) {
    Statement st = db_.prepare(
        "SELECT COALESCE(SUM(amount_paise), 0) FROM payment WHERE bill_id = ?1 AND payment_id <= ?2");
    st.bind(1, billId).bind(2, paymentId);
    st.step();
    return st.getInt64(0);
}

}  // namespace hms
