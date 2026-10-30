#pragma once
// Billing persistence (Data Access Module, billing part). SQL lives here only.

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <vector>

#include "billing/billing.hpp"
#include "data/database.hpp"

namespace hms {

struct BillableTest {
    std::int64_t orderId = 0;
    std::string testName;
};

struct NewBillLine {
    std::string kind;
    std::string description;
    std::int64_t quantity = 1;
    std::int64_t unitPricePaise = 0;
    std::optional<std::int64_t> labOrderId;
    std::int64_t amountPaise() const { return quantity * unitPricePaise; }
};

class BillingRepository {
public:
    explicit BillingRepository(Database& db) : db_(db) {}

    // Doctor fees (the Admin sets them, per doctor profile).
    bool hasDoctorProfile(std::int64_t staffId);
    void saveFee(std::int64_t staffId, std::int64_t feePaise, const std::string& nowUtc);
    std::optional<std::int64_t> feeOf(std::int64_t staffId);
    std::vector<DoctorFee> listDoctorFees();

    std::optional<std::int64_t> consultationIdForAppointment(std::int64_t appointmentId);
    std::vector<BillableTest> testsForConsultation(std::int64_t consultationId);
    std::optional<std::string> doctorUsername(std::int64_t staffId);

    std::int64_t insertBill(std::int64_t uhid, std::int64_t appointmentId, std::int64_t totalPaise,
                            const std::string& createdBy, const std::string& nowUtc);
    void insertLine(std::int64_t billId, const NewBillLine& line);

    std::optional<Bill> findBill(std::int64_t billId);
    std::optional<Bill> findBillByAppointment(std::int64_t appointmentId);
    bool patientExists(std::int64_t uhid);
    std::vector<BillSummary> listForPatient(std::int64_t uhid);
    std::vector<BillSummary> listOutstanding(std::size_t limit);  // balance > 0, oldest first

    std::int64_t insertPayment(std::int64_t billId, std::int64_t amountPaise, PaymentMode mode,
                               const std::optional<std::string>& reference, const std::string& receivedBy,
                               const std::string& nowUtc);
    std::optional<PaymentRecord> findPayment(std::int64_t paymentId);
    // Total paid on a bill up to and including a payment (for the receipt).
    std::int64_t paidUpTo(std::int64_t billId, std::int64_t paymentId);

private:
    std::optional<Bill> loadBill(const std::string& where, const std::function<void(Statement&)>& bind);

    Database& db_;
};

}  // namespace hms
