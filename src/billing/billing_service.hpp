#pragma once
// Billing & Payment Module business logic (SRS HMS-F-030..032, HMS-SR-004;
// SAD 3.4; issue #7). Follows the Appointment and Consultation contract: the
// role is checked first (a denial is audited), the input is validated, and
// each change is made in one transaction together with its audit entry.
//
//  - setConsultationFee (Admin): the fee on a doctor's profile.
//  - generateBill: one itemised bill per completed visit (an appointment whose
//    consultation has been recorded): the doctor's fee, each test the doctor
//    ordered at a price reception enters, and any "other service" lines. The
//    total is the sum of the line amounts and every bill has a unique number
//    (AC-HMS-F-030). Lines are fixed once the bill exists.
//  - recordPayment: cash, card or insurance, partial or full. A payment can
//    never exceed the outstanding balance, so the balance never goes negative
//    (AC-HMS-F-031).
//  - generateReceipt: for a recorded payment; shows the bill number, the
//    itemised charges, the payment mode and the time (AC-HMS-F-032).
//  - Audit entries hold IDs only, never names, amounts or payment references.

#include <string>
#include <vector>

#include "appointment/appointment_repository.hpp"
#include "audit/audit_log.hpp"
#include "auth/authorizer.hpp"
#include "auth/session.hpp"
#include "billing/billing.hpp"
#include "billing/billing_repository.hpp"
#include "config/policy.hpp"
#include "core/clock.hpp"
#include "core/result.hpp"
#include "data/database.hpp"

namespace hms {

class BillingService {
public:
    BillingService(Database& db, const Clock& clock, AppPolicy policy);

    // Admin: doctors with a profile, and the fee set for each (if any).
    Result<std::vector<DoctorFee>> listDoctorFees(const Session& session);
    Result<DoctorFee> setConsultationFee(const Session& session, std::int64_t doctorStaffId, const std::string& feeText);

    // Receptionist: what the bill for a visit will contain, and the bill itself.
    Result<BillPreview> previewBill(const Session& session, std::int64_t appointmentId);
    // `testPrices` holds one price per test in BillPreview::tests, in the same order.
    Result<Bill> generateBill(const Session& session, std::int64_t appointmentId,
                              const std::vector<std::string>& testPrices = {},
                              const std::vector<ExtraChargeDraft>& extraCharges = {});

    Result<Bill> getBill(const Session& session, std::int64_t billId);
    Result<Bill> findBillForAppointment(const Session& session, std::int64_t appointmentId);
    Result<std::vector<BillSummary>> listBillsForPatient(const Session& session, std::int64_t uhid);
    // Bills with a balance still to pay, oldest first.
    Result<std::vector<BillSummary>> listOutstandingBills(const Session& session, std::size_t limit = 50);

    // `reference` is optional: a transaction or claim number. A full card number is refused.
    Result<PaymentOutcome> recordPayment(const Session& session, std::int64_t billId, const std::string& amountText,
                                         PaymentMode mode, const std::string& reference = "");

    Result<Receipt> generateReceipt(const Session& session, std::int64_t paymentId);

    const AppPolicy& policy() const { return policy_; }

private:
    bool authorize(const Session& session, Permission permission);
    Error denied(const Session& session, const char* what) const;

    Database& db_;
    const Clock& clock_;
    AppPolicy policy_;
    BillingRepository repo_;
    AppointmentRepository appointments_;
    AuditLog audit_;
};

}  // namespace hms
