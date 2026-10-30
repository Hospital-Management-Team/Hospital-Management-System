#include "billing/billing_service.hpp"

#include "billing/receipt.hpp"
#include "core/money.hpp"
#include "core/text.hpp"
#include "patient/uhid.hpp"
#include "validation/input.hpp"

namespace hms {

namespace {
constexpr const char* kAmount = "Amount";
constexpr const char* kReference = "Reference";
const char* kStorageFailed = "The data store reported an error. Nothing was saved. Please try again.";
const char* kReadFailed = "The data store reported an error. Please try again.";
const char* kOutcomeUnknown =
    "The data store could not confirm whether the change was saved. Do not enter it again. "
    "Restart the application and check the bill.";

// Runs a database action and turns storage failures into the error contract.
template <class T, class F>
Result<T> guarded(bool writes, F&& action) {
    try {
        return action();
    } catch (const CommitUncertain&) {
        return writes ? Error::outcomeUnknown(kOutcomeUnknown) : Error::storageFailure(kReadFailed);
    } catch (const DbError&) {
        return Error::storageFailure(writes ? kStorageFailed : kReadFailed);
    } catch (const InjectedFault&) {
        return Error::storageFailure(writes ? kStorageFailed : kReadFailed);
    }
}

FieldError firstField(const Error& e, const std::string& field) {
    return e.fields.empty() ? FieldError{field, e.message} : e.fields.front();
}

Error billNotFound(std::int64_t id) { return Error::notFound("No bill found with number " + formatBillId(id) + "."); }

// True if the text holds 13 or more digits in one run, where spaces and hyphens do not break the
// run ("4111 1111 1111 1111"). Card numbers have 13 to 19 digits and must never be stored.
bool looksLikeCardNumber(const std::string& s) {
    int run = 0;
    for (char c : s) {
        if (c >= '0' && c <= '9') {
            if (++run >= 13) return true;
        } else if (c != ' ' && c != '-') {
            run = 0;
        }
    }
    return false;
}

Result<std::optional<std::string>> validateReference(const std::string& raw, const BillingPolicy& bp) {
    auto line = input::cleanLine(raw, kReference);
    if (!line) return line.error();
    const std::string ref = text::collapseSpaces(line.value());
    if (ref.empty()) return std::optional<std::string>{};
    if (text::utf8Length(ref) > bp.maxReferenceChars) {
        return Error::validation(kReference, "must be at most " + std::to_string(bp.maxReferenceChars) + " characters");
    }
    for (char c : ref) {
        const auto u = static_cast<unsigned char>(c);
        const bool ok = (u >= 'A' && u <= 'Z') || (u >= 'a' && u <= 'z') || (u >= '0' && u <= '9') || c == ' ' ||
                        c == '-' || c == '/' || c == '_' || c == '.';
        if (!ok) return Error::validation(kReference, "may contain only letters, digits, spaces and - / _ .");
    }
    if (looksLikeCardNumber(ref)) {
        return Error::validation(kReference, "looks like a full card number; enter a receipt or claim number instead");
    }
    return std::optional<std::string>{ref};
}

// A price typed in rupees; reports the problem under `label`.
void takePrice(std::vector<FieldError>& errors, const std::string& raw, const std::string& label, bool mustBePositive,
               std::int64_t maxPaise, std::int64_t& out) {
    auto line = input::requireLine(raw, label);
    if (!line) { errors.push_back(firstField(line.error(), label)); return; }
    auto p = input::parseMoneyPaise(line.value(), label, mustBePositive, maxPaise);
    if (!p) { errors.push_back(firstField(p.error(), label)); return; }
    out = p.value();
}
}  // namespace

BillingService::BillingService(Database& db, const Clock& clock, AppPolicy policy)
    : db_(db), clock_(clock), policy_(policy), repo_(db), appointments_(db), audit_(db, clock) {}

bool BillingService::authorize(const Session& session, Permission permission) {
    if (isAllowed(session.role, permission)) return true;
    try {
        Transaction tx(db_);
        AuditEntryInput e;
        e.action = auditActionFor(permission);
        e.outcome = AuditOutcome::Denied;
        audit_.append(session.userId, e);
        tx.commit();
    } catch (const std::exception&) {
        // The denial stands even if it could not be recorded.
    }
    return false;
}

Error BillingService::denied(const Session& session, const char* what) const {
    return Error::accessDenied("Your role (" + to_string(session.role) + ") may not " + what + ".");
}

// ------------------------------------------------------------ consultation fee

Result<std::vector<DoctorFee>> BillingService::listDoctorFees(const Session& session) {
    if (!authorize(session, Permission::SetConsultationFee)) return denied(session, "manage consultation fees");
    return guarded<std::vector<DoctorFee>>(false, [&]() -> Result<std::vector<DoctorFee>> { return repo_.listDoctorFees(); });
}

Result<DoctorFee> BillingService::setConsultationFee(const Session& session, std::int64_t doctorStaffId,
                                                      const std::string& feeText) {
    if (!authorize(session, Permission::SetConsultationFee)) return denied(session, "manage consultation fees");
    std::vector<FieldError> errors;
    std::int64_t fee = 0;
    takePrice(errors, feeText, "Consultation fee", false, policy_.billing.maxUnitPricePaise, fee);
    if (!errors.empty()) return Error::validation(std::move(errors));

    return guarded<DoctorFee>(true, [&]() -> Result<DoctorFee> {
        Transaction tx(db_);
        if (!repo_.hasDoctorProfile(doctorStaffId)) {
            return Error::notFound("That doctor has no profile yet. Set the doctor's department and hours first.");
        }
        repo_.saveFee(doctorStaffId, fee, toIsoUtc(clock_.now()));
        AuditEntryInput e;
        e.action = audit_action::kConsultationFeeSet;
        e.recordId = "STAFF-" + std::to_string(doctorStaffId);
        e.changedFields = {"consultation_fee"};
        audit_.append(session.userId, e);
        db_.faultPoint("fee_before_commit");
        tx.commit();
        for (auto& d : repo_.listDoctorFees()) {
            if (d.staffId == doctorStaffId) return d;
        }
        return Error::notFound("That doctor has no profile yet.");
    });
}

// -------------------------------------------------------------------- bills

Result<BillPreview> BillingService::previewBill(const Session& session, std::int64_t appointmentId) {
    if (!authorize(session, Permission::GenerateBill)) return denied(session, "generate bills");
    return guarded<BillPreview>(false, [&]() -> Result<BillPreview> {
        const std::string apptText = formatAppointmentId(appointmentId);
        auto appt = appointments_.findAppointment(appointmentId);
        if (!appt) return Error::notFound("No appointment found with number " + apptText + ".");
        if (appt->status == AppointmentStatus::Cancelled) {
            return Error::conflict("Appointment " + apptText + " was cancelled; it cannot be billed.");
        }
        const auto consultationId = repo_.consultationIdForAppointment(appointmentId);
        if (appt->status != AppointmentStatus::Completed || !consultationId) {
            return Error::conflict("The visit is not complete: the doctor must record the consultation for appointment " +
                                   apptText + " before it can be billed.");
        }
        if (auto existing = repo_.findBillByAppointment(appointmentId)) {
            return Error::conflict("Appointment " + apptText + " already has bill " + formatBillId(existing->billId) + ".");
        }
        const auto fee = repo_.feeOf(appt->doctorStaffId);
        if (!fee) {
            return Error::conflict("No consultation fee has been set for Dr " + appt->doctorUsername +
                                   ". Ask an Administrator to set it.");
        }
        BillPreview p;
        p.appointmentId = appointmentId;
        p.uhid = appt->uhid;
        p.patientName = appt->patientName;
        p.doctorName = appt->doctorUsername;
        p.consultationFeePaise = *fee;
        for (const auto& t : repo_.testsForConsultation(*consultationId)) p.tests.push_back(t.testName);
        return p;
    });
}

Result<Bill> BillingService::generateBill(const Session& session, std::int64_t appointmentId,
                                          const std::vector<std::string>& testPrices,
                                          const std::vector<ExtraChargeDraft>& extraCharges) {
    if (!authorize(session, Permission::GenerateBill)) return denied(session, "generate bills");
    const BillingPolicy& bp = policy_.billing;

    // Validate the typed prices and service lines first; report every bad field at once.
    std::vector<FieldError> errors;
    if (appointmentId <= 0) errors.push_back(FieldError{"Appointment", "must be chosen"});
    if (extraCharges.size() > bp.maxExtraLines) {
        errors.push_back(FieldError{"Service charges", "has too many lines (at most " + std::to_string(bp.maxExtraLines) + ")"});
    }
    std::vector<std::int64_t> prices;
    for (std::size_t i = 0; i < testPrices.size(); ++i) {
        std::int64_t price = 0;
        takePrice(errors, testPrices[i], "Test " + std::to_string(i + 1) + " price", false, bp.maxUnitPricePaise, price);
        prices.push_back(price);
    }
    std::vector<NewBillLine> extras;
    for (std::size_t i = 0; i < extraCharges.size() && i < bp.maxExtraLines; ++i) {
        const auto& x = extraCharges[i];
        const std::string label = "Service line " + std::to_string(i + 1);
        NewBillLine line;
        line.kind = "SERVICE";
        auto d = input::requireLine(x.description, label + " description");
        if (!d) {
            errors.push_back(firstField(d.error(), label + " description"));
        } else {
            line.description = text::collapseSpaces(d.value());
            if (text::utf8Length(line.description) > bp.maxDescriptionChars) {
                errors.push_back(FieldError{label + " description",
                                            "must be at most " + std::to_string(bp.maxDescriptionChars) + " characters"});
            }
        }
        auto qty = input::cleanLine(x.quantity, label + " quantity");
        if (!qty) {
            errors.push_back(firstField(qty.error(), label + " quantity"));
        } else if (!qty.value().empty()) {
            auto n = input::parseWholeNumber(qty.value(), label + " quantity", 1, bp.maxLineQuantity);
            if (!n) errors.push_back(firstField(n.error(), label + " quantity"));
            else line.quantity = n.value();
        }
        takePrice(errors, x.unitPrice, label + " unit price", true, bp.maxUnitPricePaise, line.unitPricePaise);
        extras.push_back(std::move(line));
    }
    if (!errors.empty()) return Error::validation(std::move(errors));

    return guarded<Bill>(true, [&]() -> Result<Bill> {
        Transaction tx(db_);
        const std::string apptText = formatAppointmentId(appointmentId);
        auto appt = appointments_.findAppointment(appointmentId);
        if (!appt) return Error::notFound("No appointment found with number " + apptText + ".");
        if (appt->status == AppointmentStatus::Cancelled) {
            return Error::conflict("Appointment " + apptText + " was cancelled; it cannot be billed.");
        }
        const auto consultationId = repo_.consultationIdForAppointment(appointmentId);
        if (appt->status != AppointmentStatus::Completed || !consultationId) {
            return Error::conflict("The visit is not complete: the doctor must record the consultation for appointment " +
                                   apptText + " before it can be billed.");
        }
        if (auto existing = repo_.findBillByAppointment(appointmentId)) {
            return Error::conflict("Appointment " + apptText + " already has bill " + formatBillId(existing->billId) + ".");
        }
        const auto fee = repo_.feeOf(appt->doctorStaffId);
        if (!fee) {
            return Error::conflict("No consultation fee has been set for Dr " + appt->doctorUsername +
                                   ". Ask an Administrator to set it.");
        }
        const auto tests = repo_.testsForConsultation(*consultationId);
        if (tests.size() != prices.size()) {
            return Error::validation("Test prices", "need one price for each of the " + std::to_string(tests.size()) +
                                                        " test(s) the doctor ordered");
        }

        std::vector<NewBillLine> lines;
        NewBillLine consult;
        consult.kind = "CONSULTATION";
        consult.description = "Consultation - Dr " + appt->doctorUsername +
                              (appt->department.empty() ? "" : " (" + appt->department + ")");
        consult.quantity = 1;
        consult.unitPricePaise = *fee;
        lines.push_back(std::move(consult));
        for (std::size_t i = 0; i < tests.size(); ++i) {
            NewBillLine t;
            t.kind = "LAB_TEST";
            t.description = "Test - " + tests[i].testName;
            t.quantity = 1;
            t.unitPricePaise = prices[i];
            t.labOrderId = tests[i].orderId;
            lines.push_back(std::move(t));
        }
        for (auto& x : extras) lines.push_back(x);

        std::int64_t total = 0;
        for (const auto& l : lines) total += l.amountPaise();

        const std::int64_t billId = repo_.insertBill(appt->uhid, appointmentId, total, session.userId, toIsoUtc(clock_.now()));
        for (const auto& l : lines) repo_.insertLine(billId, l);

        AuditEntryInput e;
        e.action = audit_action::kBillGenerate;
        e.recordId = formatBillId(billId);
        e.changedFields = {"bill", "bill_lines", "total"};
        audit_.append(session.userId, e);
        db_.faultPoint("bill_before_commit");
        tx.commit();
        return *repo_.findBill(billId);
    });
}

Result<Bill> BillingService::getBill(const Session& session, std::int64_t billId) {
    if (!authorize(session, Permission::ViewBills)) return denied(session, "view bills");
    return guarded<Bill>(false, [&]() -> Result<Bill> {
        auto b = repo_.findBill(billId);
        if (!b) return billNotFound(billId);
        return *b;
    });
}

Result<Bill> BillingService::findBillForAppointment(const Session& session, std::int64_t appointmentId) {
    if (!authorize(session, Permission::ViewBills)) return denied(session, "view bills");
    return guarded<Bill>(false, [&]() -> Result<Bill> {
        auto b = repo_.findBillByAppointment(appointmentId);
        if (!b) return Error::notFound("Appointment " + formatAppointmentId(appointmentId) + " has no bill yet.");
        return *b;
    });
}

Result<std::vector<BillSummary>> BillingService::listBillsForPatient(const Session& session, std::int64_t uhid) {
    if (!authorize(session, Permission::ViewBills)) return denied(session, "view bills");
    return guarded<std::vector<BillSummary>>(false, [&]() -> Result<std::vector<BillSummary>> {
        if (!repo_.patientExists(uhid)) return Error::notFound("No patient found with UHID " + formatUhid(uhid) + ".");
        return repo_.listForPatient(uhid);
    });
}

Result<std::vector<BillSummary>> BillingService::listOutstandingBills(const Session& session, std::size_t limit) {
    if (!authorize(session, Permission::ViewBills)) return denied(session, "view bills");
    return guarded<std::vector<BillSummary>>(false, [&]() -> Result<std::vector<BillSummary>> {
        return repo_.listOutstanding(limit);
    });
}

// ----------------------------------------------------------------- payments

Result<PaymentOutcome> BillingService::recordPayment(const Session& session, std::int64_t billId,
                                                     const std::string& amountText, PaymentMode mode,
                                                     const std::string& reference) {
    if (!authorize(session, Permission::RecordPayment)) return denied(session, "record payments");
    std::vector<FieldError> errors;
    std::int64_t amount = 0;
    // The upper bound is the largest bill the policy allows; the balance check below is the real limit.
    const std::int64_t cap = policy_.billing.maxUnitPricePaise * policy_.billing.maxLineQuantity * 100;
    takePrice(errors, amountText, kAmount, true, cap, amount);
    std::optional<std::string> ref;
    auto r = validateReference(reference, policy_.billing);
    if (!r) errors.push_back(firstField(r.error(), kReference));
    else ref = r.value();
    if (!errors.empty()) return Error::validation(std::move(errors));

    return guarded<PaymentOutcome>(true, [&]() -> Result<PaymentOutcome> {
        Transaction tx(db_);
        auto bill = repo_.findBill(billId);
        if (!bill) return billNotFound(billId);
        if (bill->balancePaise() <= 0) {
            return Error::conflict("Bill " + formatBillId(billId) + " is already paid in full.");
        }
        if (amount > bill->balancePaise()) {
            return Error::conflict("The amount " + formatRupees(amount) + " is more than the outstanding balance " +
                                   formatRupees(bill->balancePaise()) + " of bill " + formatBillId(billId) + ".");
        }
        const std::int64_t paymentId = repo_.insertPayment(billId, amount, mode, ref, session.userId, toIsoUtc(clock_.now()));
        AuditEntryInput e;
        e.action = audit_action::kPaymentRecord;
        e.recordId = formatReceiptId(paymentId);
        e.changedFields = {"payment", "balance"};
        audit_.append(session.userId, e);
        db_.faultPoint("payment_before_commit");
        tx.commit();

        PaymentOutcome out;
        out.payment = *repo_.findPayment(paymentId);
        out.balanceAfterPaise = bill->balancePaise() - amount;
        out.status = out.balanceAfterPaise == 0 ? BillStatus::Paid : BillStatus::PartiallyPaid;
        return out;
    });
}

Result<Receipt> BillingService::generateReceipt(const Session& session, std::int64_t paymentId) {
    if (!authorize(session, Permission::GenerateReceipt)) return denied(session, "generate receipts");
    return guarded<Receipt>(true, [&]() -> Result<Receipt> {
        Transaction tx(db_);
        auto payment = repo_.findPayment(paymentId);
        if (!payment) return Error::notFound("No payment found with receipt number " + formatReceiptId(paymentId) + ".");
        auto bill = repo_.findBill(payment->billId);
        if (!bill) return billNotFound(payment->billId);

        Receipt r;
        r.receiptNumber = formatReceiptId(paymentId);
        r.payment = *payment;
        r.uhid = bill->uhid;
        r.patientName = bill->patientName;
        r.appointmentId = bill->appointmentId;
        r.slotStart = bill->slotStart;
        r.doctorName = bill->doctorName;
        r.lines = bill->lines;
        r.billTotalPaise = bill->totalPaise;
        r.paidToDatePaise = repo_.paidUpTo(bill->billId, paymentId);
        r.balanceAfterPaise = bill->totalPaise - r.paidToDatePaise;
        r.paidAtLocal = localDateTimeText(payment->paidAt, policy_.utcOffsetMinutes);

        // A receipt print is audited (who printed which receipt); nothing else changes.
        AuditEntryInput e;
        e.action = audit_action::kReceiptGenerate;
        e.recordId = r.receiptNumber;
        audit_.append(session.userId, e);
        tx.commit();
        return r;
    });
}

}  // namespace hms
