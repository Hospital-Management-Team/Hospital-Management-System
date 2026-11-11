// Billing screens for the Receptionist: bill a completed visit (HMS-F-030), record a payment and
// print its receipt (HMS-F-031, F-032), bills and balances, and a receipt printed again.

#include <algorithm>

#include "appointment/appointment.hpp"
#include "billing/receipt.hpp"
#include "core/money.hpp"
#include "gui/patient_picker.hpp"
#include "gui/screens.hpp"
#include "imgui_stdlib.h"
#include "patient/uhid.hpp"

namespace hms::gui {

namespace {

float tableHeight(std::size_t rows, std::size_t most = 10) { return rowsHeight(rows, most); }

void drawLines(const std::vector<BillLine>& lines) {
    if (!beginTable("lines", {"Description", "Qty", "Unit price", "Amount"}, tableHeight(lines.size()))) return;
    for (const auto& l : lines) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextUnformatted(l.description.c_str());
        ImGui::TableSetColumnIndex(1);
        ImGui::TextUnformatted(std::to_string(l.quantity).c_str());
        ImGui::TableSetColumnIndex(2);
        ImGui::TextUnformatted(formatRupees(l.unitPricePaise).c_str());
        ImGui::TableSetColumnIndex(3);
        ImGui::TextUnformatted(formatRupees(l.amountPaise).c_str());
    }
    ImGui::EndTable();
}

void drawBill(const Bill& b, int utcOffset) {
    keyValue("Bill", formatBillId(b.billId));
    keyValue("Patient", b.patientName + " (" + formatUhid(b.uhid) + ")");
    keyValue("Appointment", formatAppointmentId(b.appointmentId) + ", Dr " + b.doctorName);
    keyValue("Created", localDateTimeText(b.createdAt, utcOffset) + " by " + b.createdBy);
    vspace();
    drawLines(b.lines);
    vspace();
    keyValue("Total", formatRupees(b.totalPaise));
    keyValue("Paid", formatRupees(b.paidPaise));
    keyValue("Balance", formatRupees(b.balancePaise()));
    keyValue("Status", to_string(b.status()));
    if (!b.payments.empty()) {
        sectionTitle("Payments");
        if (beginTable("payments", {"Receipt", "Amount", "Mode", "Reference", "Received by", "When"}, tableHeight(b.payments.size()))) {
            for (const auto& p : b.payments) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(formatReceiptId(p.paymentId).c_str());
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(formatRupees(p.amountPaise).c_str());
                ImGui::TableSetColumnIndex(2);
                ImGui::TextUnformatted(paymentModeLabel(p.mode).c_str());
                ImGui::TableSetColumnIndex(3);
                ImGui::TextUnformatted(p.reference.value_or("-").c_str());
                ImGui::TableSetColumnIndex(4);
                ImGui::TextUnformatted(p.receivedBy.c_str());
                ImGui::TableSetColumnIndex(5);
                ImGui::TextUnformatted(localDateTimeText(p.paidAt, utcOffset).c_str());
            }
            ImGui::EndTable();
        }
    }
}

void drawReceipt(const Receipt& r) {
    keyValue("Receipt", r.receiptNumber);
    keyValue("Paid on", r.paidAtLocal);
    keyValue("Patient", r.patientName + " (" + formatUhid(r.uhid) + ")");
    keyValue("Appointment", formatAppointmentId(r.appointmentId) + ", Dr " + r.doctorName);
    vspace();
    drawLines(r.lines);
    vspace();
    keyValue("Bill total", formatRupees(r.billTotalPaise));
    keyValue("This payment", formatRupees(r.payment.amountPaise) + " (" + paymentModeLabel(r.payment.mode) +
                                 (r.payment.reference ? ", " + *r.payment.reference : std::string()) + ")");
    keyValue("Paid to date", formatRupees(r.paidToDatePaise));
    keyValue("Balance", formatRupees(r.balanceAfterPaise));
    keyValue("Received by", r.payment.receivedBy);
}

// ---- Bill a completed visit -----------------------------------------------------------------

class BillVisitScreen final : public Screen {
public:
    void draw(Context& ctx) override {
        heading("Bill a completed visit");
        notice_.draw();
        switch (step_) {
            case Step::Pick: drawPick(ctx); break;
            case Step::Form: drawForm(ctx); break;
            case Step::Done: drawDone(ctx); break;
        }
    }

private:
    enum class Step { Pick, Form, Done };

    void drawPick(Context& ctx) {
        muted("The visit must be completed (the doctor has recorded the consultation). A bill cannot be changed afterwards.");
        vspace();
        const bool go = textField("Appointment number (for example APT-000012)", appointmentText_, nullptr,
                                  ImGuiInputTextFlags_EnterReturnsTrue);
        if (primaryButton("Continue") || go) preview(ctx);
    }

    void preview(Context& ctx) {
        notice_.clear();
        auto id = parseAppointmentId(appointmentText_);
        if (!id) {
            notice_.error("Appointment: must look like APT-000012 or 12");
            return;
        }
        auto r = ctx.svc.billing.previewBill(ctx.session, *id);
        if (!r) {
            ctx.fail(notice_, r.error());
            return;
        }
        preview_ = r.value();
        prices_.assign(preview_.tests.size(), std::string());
        extras_.clear();
        confirming_ = false;
        step_ = Step::Form;
    }

    void drawForm(Context& ctx) {
        const auto& bp = ctx.svc.policy.billing;
        ImGui::TextWrapped("%s  %s (%s)  Dr %s", formatAppointmentId(preview_.appointmentId).c_str(), preview_.patientName.c_str(),
                           formatUhid(preview_.uhid).c_str(), preview_.doctorName.c_str());
        keyValue("Consultation fee", formatRupees(preview_.consultationFeePaise));
        if (!preview_.tests.empty()) {
            sectionTitle("Price of each test (0 if no charge)");
            for (std::size_t i = 0; i < preview_.tests.size(); ++i) {
                ImGui::PushID(static_cast<int>(i));
                ImGui::SetNextItemWidth(ImGui::GetFontSize() * 9.0F);
                ImGui::InputTextWithHint("##price", "Rs", &prices_[i]);
                ImGui::SameLine();
                ImGui::TextUnformatted(preview_.tests[i].c_str());
                ImGui::PopID();
            }
        }
        sectionTitle("Other service charges (for example a dressing)");
        for (std::size_t i = 0; i < extras_.size(); ++i) {
            auto& x = extras_[i];
            ImGui::PushID(2000 + static_cast<int>(i));
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 16.0F);
            ImGui::InputTextWithHint("##desc", "Description", &x.description);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 4.0F);
            ImGui::InputTextWithHint("##qty", "Qty", &x.quantity);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8.0F);
            ImGui::InputTextWithHint("##unit", "Unit price Rs", &x.unitPrice);
            ImGui::SameLine();
            if (plainButton("Remove")) {
                extras_.erase(extras_.begin() + static_cast<std::ptrdiff_t>(i));
                ImGui::PopID();
                break;
            }
            ImGui::PopID();
        }
        if (extras_.size() < bp.maxExtraLines && plainButton("Add a service charge")) extras_.emplace_back();

        vspace(2.0F);
        if (!confirming_) {
            if (primaryButton("Create the bill")) confirming_ = true;
            ImGui::SameLine();
            if (plainButton("Cancel")) {
                step_ = Step::Pick;
                notice_.info("Cancelled. Nothing was saved.");
            }
            return;
        }
        ImGui::TextUnformatted("Create the bill? It cannot be changed afterwards.");
        if (primaryButton("Yes, create the bill")) {
            notice_.clear();
            if (ctx.blocked(notice_)) return;
            auto r = ctx.svc.billing.generateBill(ctx.session, preview_.appointmentId, prices_, extras_);
            confirming_ = false;
            if (!r) {
                ctx.fail(notice_, r.error());
                return;
            }
            bill_ = r.value();
            notice_.info("Bill " + formatBillId(bill_.billId) + " created.");
            step_ = Step::Done;
        }
        ImGui::SameLine();
        if (plainButton("No, go back")) confirming_ = false;
    }

    void drawDone(Context& ctx) {
        drawBill(bill_, ctx.svc.billing.policy().utcOffsetMinutes);
        vspace();
        if (primaryButton("Record a payment")) {
            ctx.billForNext = bill_.billId;
            ctx.goTo = "Payments";
        }
        ImGui::SameLine();
        if (plainButton("Bill another visit")) {
            appointmentText_.clear();
            notice_.clear();
            step_ = Step::Pick;
        }
    }

    Step step_ = Step::Pick;
    std::string appointmentText_;
    BillPreview preview_;
    std::vector<std::string> prices_;
    std::vector<ExtraChargeDraft> extras_;
    bool confirming_ = false;
    Bill bill_;
    Notice notice_;
};

// ---- Record a payment ---------------------------------------------------------------------------

class PaymentScreen final : public Screen {
public:
    void onOpen(Context& ctx) override {
        if (ctx.billForNext != 0) {
            billText_ = formatBillId(ctx.billForNext);
            ctx.billForNext = 0;
            notice_.clear();
            receiptShown_ = false;
            load(ctx);
        }
    }

    void draw(Context& ctx) override {
        heading("Record a payment");
        notice_.draw();
        const bool go = textField("Bill number (for example BILL-000012)", billText_, nullptr, ImGuiInputTextFlags_EnterReturnsTrue);
        if (primaryButton("Open the bill") || go) {
            notice_.clear();
            receiptShown_ = false;
            load(ctx);
        }
        if (!loaded_) return;

        const int offset = ctx.svc.billing.policy().utcOffsetMinutes;
        sectionTitle("Bill");
        drawBill(bill_, offset);

        if (bill_.balancePaise() > 0) {
            sectionTitle("Payment received");
            textField(("Amount in rupees (outstanding " + formatRupees(bill_.balancePaise()) + ")").c_str(), amount_);
            ImGui::TextDisabled("Payment mode");
            ImGui::RadioButton("Cash", &mode_, 0);
            ImGui::SameLine();
            ImGui::RadioButton("Card", &mode_, 1);
            ImGui::SameLine();
            ImGui::RadioButton("Insurance", &mode_, 2);
            textField("Reference (receipt or claim number, optional; never a card number)", reference_);
            vspace();
            if (primaryButton("Record the payment")) record(ctx);
        } else if (!receiptShown_) {
            muted("This bill is paid in full.");
        }

        if (receiptShown_) {
            sectionTitle("Receipt");
            drawReceipt(receipt_);
        }
    }

private:
    void load(Context& ctx) {
        loaded_ = false;
        auto id = parseBillId(billText_);
        if (!id) {
            notice_.error("Bill: must look like BILL-000012 or 12");
            return;
        }
        auto r = ctx.svc.billing.getBill(ctx.session, *id);
        if (!r) {
            ctx.fail(notice_, r.error());
            return;
        }
        bill_ = r.value();
        loaded_ = true;
    }

    void record(Context& ctx) {
        notice_.clear();
        if (ctx.blocked(notice_)) return;
        const PaymentMode mode = mode_ == 0 ? PaymentMode::Cash : (mode_ == 1 ? PaymentMode::Card : PaymentMode::Insurance);
        auto outcome = ctx.svc.billing.recordPayment(ctx.session, bill_.billId, amount_, mode, reference_);
        if (!outcome) {
            ctx.fail(notice_, outcome.error());
            return;
        }
        const PaymentOutcome& o = outcome.value();
        amount_.clear();
        reference_.clear();
        auto receipt = ctx.svc.billing.generateReceipt(ctx.session, o.payment.paymentId);
        notice_.info("Payment of " + formatRupees(o.payment.amountPaise) + " recorded. Balance " + formatRupees(o.balanceAfterPaise) +
                     " (" + to_string(o.status) + ").");
        if (receipt) {
            receipt_ = receipt.value();
            receiptShown_ = true;
        } else {
            ctx.fail(notice_, receipt.error());
        }
        load(ctx);
    }

    Notice notice_;
    std::string billText_;
    Bill bill_;
    bool loaded_ = false;
    std::string amount_;
    std::string reference_;
    int mode_ = 0;
    Receipt receipt_;
    bool receiptShown_ = false;
};

// ---- Bills and balances -------------------------------------------------------------------------

class BillsScreen final : public Screen {
public:
    void draw(Context& ctx) override {
        heading("Bills and balances");
        notice_.draw();
        ImGui::RadioButton("Bills of one patient", &mode_, 0);
        ImGui::SameLine();
        ImGui::RadioButton("All outstanding bills", &mode_, 1);
        vspace();
        if (mode_ == 0) {
            if (picker_.draw(ctx)) load(ctx);
            if (picker_.patient() && primaryButton("Show the bills")) load(ctx);
        } else if (primaryButton("Show the outstanding bills")) {
            load(ctx);
        }
        if (!loaded_) return;

        sectionTitle(title_.c_str());
        if (rows_.empty()) {
            muted(mode_ == 0 ? "This patient has no bills." : "No bill is outstanding.");
            return;
        }
        if (beginTable("bills", {"Bill", "Patient", "Total", "Paid", "Balance"}, tableHeight(rows_.size()))) {
            for (std::size_t i = 0; i < rows_.size(); ++i) {
                const auto& b = rows_[i];
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::PushID(static_cast<int>(i));
                if (ImGui::Selectable(formatBillId(b.billId).c_str(), selected_ == static_cast<int>(i),
                                      ImGuiSelectableFlags_SpanAllColumns)) {
                    selected_ = static_cast<int>(i);
                    openBill(ctx, b.billId);
                }
                ImGui::PopID();
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted((b.patientName + " " + formatUhid(b.uhid)).c_str());
                ImGui::TableSetColumnIndex(2);
                ImGui::TextUnformatted(formatRupees(b.totalPaise).c_str());
                ImGui::TableSetColumnIndex(3);
                ImGui::TextUnformatted(formatRupees(b.paidPaise).c_str());
                ImGui::TableSetColumnIndex(4);
                ImGui::TextUnformatted(formatRupees(b.balancePaise()).c_str());
            }
            ImGui::EndTable();
        }
        if (selectedBill_) {
            sectionTitle("Bill");
            drawBill(*selectedBill_, ctx.svc.billing.policy().utcOffsetMinutes);
            if (selectedBill_->balancePaise() > 0) {
                vspace();
                if (primaryButton("Record a payment")) {
                    ctx.billForNext = selectedBill_->billId;
                    ctx.goTo = "Payments";
                }
            }
        }
    }

private:
    void load(Context& ctx) {
        notice_.clear();
        loaded_ = false;
        selected_ = -1;
        selectedBill_.reset();
        Result<std::vector<BillSummary>> r = mode_ == 0 ? ctx.svc.billing.listBillsForPatient(ctx.session, picker_.uhid())
                                                       : ctx.svc.billing.listOutstandingBills(ctx.session);
        if (!r) {
            ctx.fail(notice_, r.error());
            return;
        }
        rows_ = r.value();
        title_ = mode_ == 0 ? "Bills of " + (picker_.patient() ? picker_.patient()->fullName : std::string())
                            : std::string("Outstanding bills");
        loaded_ = true;
    }

    void openBill(Context& ctx, std::int64_t billId) {
        auto r = ctx.svc.billing.getBill(ctx.session, billId);
        if (!r) {
            ctx.fail(notice_, r.error());
            return;
        }
        selectedBill_ = r.value();
    }

    PatientPicker picker_;
    Notice notice_;
    int mode_ = 0;
    bool loaded_ = false;
    std::string title_;
    std::vector<BillSummary> rows_;
    int selected_ = -1;
    std::optional<Bill> selectedBill_;
};

// ---- Print a receipt again ----------------------------------------------------------------------

class ReceiptScreen final : public Screen {
public:
    void draw(Context& ctx) override {
        heading("Print a receipt again");
        notice_.draw();
        const bool go = textField("Receipt number (for example RCPT-000012)", text_, nullptr, ImGuiInputTextFlags_EnterReturnsTrue);
        if (primaryButton("Show the receipt") || go) {
            notice_.clear();
            shown_ = false;
            auto id = parseReceiptId(text_);
            if (!id) {
                notice_.error("Receipt: must look like RCPT-000012 or 12");
            } else {
                auto r = ctx.svc.billing.generateReceipt(ctx.session, *id);
                if (!r) {
                    ctx.fail(notice_, r.error());
                } else {
                    receipt_ = r.value();
                    shown_ = true;
                }
            }
        }
        if (shown_) {
            vspace();
            drawReceipt(receipt_);
        }
    }

private:
    Notice notice_;
    std::string text_;
    Receipt receipt_;
    bool shown_ = false;
};

}  // namespace

std::unique_ptr<Screen> makeBillVisitScreen() { return std::make_unique<BillVisitScreen>(); }
std::unique_ptr<Screen> makePaymentScreen() { return std::make_unique<PaymentScreen>(); }
std::unique_ptr<Screen> makeBillsScreen() { return std::make_unique<BillsScreen>(); }
std::unique_ptr<Screen> makeReceiptScreen() { return std::make_unique<ReceiptScreen>(); }

}  // namespace hms::gui
