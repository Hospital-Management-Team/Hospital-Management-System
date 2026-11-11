// Laboratory screens (HMS-F-040: results) and pharmacy screens (HMS-F-041: stock, F-042: dispensing).

#include <algorithm>
#include <cctype>
#include <functional>
#include <map>

#include "billing/receipt.hpp"
#include "core/text.hpp"
#include "gui/patient_picker.hpp"
#include "gui/screens.hpp"
#include "imgui_stdlib.h"
#include "patient/uhid.hpp"
#include "validation/input.hpp"

namespace hms::gui {

namespace {

float tableHeight(std::size_t rows, std::size_t most = 10) { return rowsHeight(rows, most); }

// True when the stock item looks like the prescribed medicine: the same text, one name inside the
// other ("Paracetamol" and "Paracetamol 500 mg"), or the same first word. Case is ignored.
bool sameMedicine(const std::string& prescribed, const std::string& stock) {
    auto lower = [](std::string s) {
        for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        return text::trimSpaces(s);
    };
    auto firstWord = [](const std::string& s) { return s.substr(0, s.find(' ')); };
    const std::string a = lower(prescribed);
    const std::string b = lower(stock);
    if (a.empty() || b.empty()) return true;
    return a.find(b) != std::string::npos || b.find(a) != std::string::npos || firstWord(a) == firstWord(b);
}

void drawOrderDetail(const LabOrderView& o, int offset) {
    keyValue("Order", formatLabOrderId(o.orderId));
    keyValue("Patient", o.patientName + " (" + formatUhid(o.uhid) + ")");
    keyValue("Test", o.testName + (o.instructions ? " (" + *o.instructions + ")" : std::string()));
    keyValue("Ordered", localDateTimeText(o.orderedAt, offset) + " by Dr " + o.doctorUsername);
    if (o.latest) {
        keyValue("Result", o.latest->text);
        keyValue("Revision", std::to_string(o.latest->revision) + ", entered " + localDateTimeText(o.latest->enteredAt, offset) +
                                 " by " + o.latest->enteredBy);
    } else {
        keyValue("Result", "(none yet)");
    }
}

// ---- Lab: tests waiting, enter or correct a result ---------------------------------------------

class LabPendingScreen final : public Screen {
public:
    void onOpen(Context& ctx) override { load(ctx); }

    void draw(Context& ctx) override {
        heading("Tests waiting for a result");
        notice_.draw();
        if (plainButton("Refresh the list")) load(ctx);
        vspace();
        if (rows_.empty()) {
            muted("None. Every ordered test has a result.");
            return;
        }
        const int offset = ctx.svc.lab.policy().utcOffsetMinutes;
        if (beginTable("pending", {"Order", "UHID", "Patient", "Test", "Ordered", "Doctor"}, tableHeight(rows_.size()))) {
            for (std::size_t i = 0; i < rows_.size(); ++i) {
                const auto& o = rows_[i];
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::PushID(static_cast<int>(i));
                if (ImGui::Selectable(formatLabOrderId(o.orderId).c_str(), selected_ == static_cast<int>(i),
                                      ImGuiSelectableFlags_SpanAllColumns)) {
                    selected_ = static_cast<int>(i);
                    result_.clear();
                }
                ImGui::PopID();
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(formatUhid(o.uhid).c_str());
                ImGui::TableSetColumnIndex(2);
                ImGui::TextUnformatted(o.patientName.c_str());
                ImGui::TableSetColumnIndex(3);
                ImGui::TextUnformatted(o.testName.c_str());
                ImGui::TableSetColumnIndex(4);
                ImGui::TextUnformatted(localDateTimeText(o.orderedAt, offset).c_str());
                ImGui::TableSetColumnIndex(5);
                ImGui::TextUnformatted(("Dr " + o.doctorUsername).c_str());
            }
            ImGui::EndTable();
        }
        if (selected_ < 0 || selected_ >= static_cast<int>(rows_.size())) return;
        const LabOrderView o = rows_[static_cast<std::size_t>(selected_)];
        sectionTitle("Enter the result");
        drawOrderDetail(o, offset);
        vspace();
        multilineField("Result", result_, 4.0F);
        if (primaryButton("Save the result")) {
            notice_.clear();
            if (ctx.blocked(notice_)) return;
            auto r = ctx.svc.lab.recordResult(ctx.session, o.orderId, result_);
            if (!r) {
                ctx.fail(notice_, r.error());
                return;
            }
            notice_.info("Saved as revision " + std::to_string(r.value().latest ? r.value().latest->revision : 1) + " of " +
                         formatLabOrderId(o.orderId) + ". The treating doctor can now see it.");
            result_.clear();
            load(ctx);
        }
    }

private:
    void load(Context& ctx) {
        auto r = ctx.svc.lab.listPendingOrders(ctx.session);
        selected_ = -1;
        if (!r) {
            ctx.fail(notice_, r.error());
            rows_.clear();
            return;
        }
        rows_ = r.value();
    }

    Notice notice_;
    std::vector<LabOrderView> rows_;
    int selected_ = -1;
    std::string result_;
};

// ---- Lab: the tests of one patient, and correcting a result -------------------------------------

class LabPatientScreen final : public Screen {
public:
    void draw(Context& ctx) override {
        heading("Tests of a patient");
        notice_.draw();
        if (picker_.draw(ctx)) load(ctx);
        if (!picker_.hasUhid()) return;
        if (primaryButton("Show the tests")) load(ctx);
        vspace();
        if (!loaded_) return;
        if (rows_.empty()) {
            muted("No tests ordered.");
            return;
        }
        const int offset = ctx.svc.lab.policy().utcOffsetMinutes;
        for (const auto& o : rows_) {
            const std::string header = formatLabOrderId(o.orderId) + "   " + o.testName + (o.hasResult() ? "" : "   (no result yet)") +
                                       "###t" + std::to_string(o.orderId);
            if (!ImGui::CollapsingHeader(header.c_str())) continue;
            drawOrderDetail(o, offset);
            ImGui::PushID(static_cast<int>(o.orderId));
            std::string& text = edits_[o.orderId];
            multilineField(o.hasResult() ? "New result text (saves a new revision)" : "Result", text, 3.0F);
            if (primaryButton("Save the result")) {
                notice_.clear();
                if (!ctx.blocked(notice_)) {
                    auto r = ctx.svc.lab.recordResult(ctx.session, o.orderId, text);
                    if (!r) {
                        ctx.fail(notice_, r.error());
                    } else {
                        notice_.info("Saved as revision " + std::to_string(r.value().latest ? r.value().latest->revision : 1) +
                                     " of " + formatLabOrderId(o.orderId) + ".");
                        text.clear();
                        ImGui::PopID();
                        load(ctx);
                        return;
                    }
                }
            }
            ImGui::PopID();
        }
    }

private:
    void load(Context& ctx) {
        loaded_ = false;
        rows_.clear();
        if (!picker_.hasUhid()) return;
        auto r = ctx.svc.lab.listOrdersForPatient(ctx.session, picker_.uhid());
        if (!r) {
            ctx.fail(notice_, r.error());
            return;
        }
        rows_ = r.value();
        loaded_ = true;
    }

    PatientPicker picker_{false};
    Notice notice_;
    bool loaded_ = false;
    std::vector<LabOrderView> rows_;
    std::map<std::int64_t, std::string> edits_;
};

// ---- Pharmacy: medicine stock ---------------------------------------------------------------------

class StockScreen final : public Screen {
public:
    void onOpen(Context& ctx) override { load(ctx); }

    void draw(Context& ctx) override {
        heading("Medicine stock");
        notice_.draw();
        const std::int64_t threshold = ctx.svc.pharmacy.lowStockThreshold();
        muted(("A medicine is shown as LOW when fewer than " + std::to_string(threshold) + " units are left.").c_str());
        if (plainButton("Refresh")) load(ctx);
        vspace();
        if (rows_.empty()) {
            muted("No medicine is in the stock list yet. Add one below.");
        } else if (beginTable("stock", {"Medicine number", "Name", "Unit", "In stock", "Status"}, tableHeight(rows_.size(), 12))) {
            for (std::size_t i = 0; i < rows_.size(); ++i) {
                const auto& m = rows_[i];
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::PushID(static_cast<int>(i));
                if (ImGui::Selectable(formatMedicineId(m.medicineId).c_str(), selected_ == static_cast<int>(i),
                                      ImGuiSelectableFlags_SpanAllColumns)) {
                    selected_ = static_cast<int>(i);
                    editName_ = m.name;
                    editUnit_ = m.unit;
                    counted_.clear();
                    reason_.clear();
                    restock_.clear();
                }
                ImGui::PopID();
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(m.name.c_str());
                ImGui::TableSetColumnIndex(2);
                ImGui::TextUnformatted(m.unit.c_str());
                ImGui::TableSetColumnIndex(3);
                ImGui::TextUnformatted(std::to_string(m.stockQuantity).c_str());
                ImGui::TableSetColumnIndex(4);
                ImGui::TextUnformatted(!m.active ? "unavailable" : (m.lowStock ? "LOW STOCK" : "ok"));
            }
            ImGui::EndTable();
        }
        if (selected_ >= 0 && selected_ < static_cast<int>(rows_.size())) drawSelected(ctx);
        drawAdd(ctx);
    }

private:
    void drawSelected(Context& ctx) {
        const Medicine m = rows_[static_cast<std::size_t>(selected_)];
        sectionTitle((m.name + " (" + formatMedicineId(m.medicineId) + ")").c_str());
        textField(("Quantity received (" + m.unit + ")").c_str(), restock_);
        if (primaryButton("Restock")) act(ctx, [&]() { return ctx.svc.pharmacy.restock(ctx.session, m.medicineId, restock_); }, "restocked");
        vspace();
        textField("Correct name", editName_);
        textField("Correct unit", editUnit_);
        if (plainButton("Save the name and unit")) {
            act(ctx, [&]() { return ctx.svc.pharmacy.updateMedicine(ctx.session, m.medicineId, MedicineEdit{editName_, editUnit_}); },
                "updated");
        }
        vspace();
        textField("Quantity actually counted on the shelf", counted_);
        textField("Reason (for example broken strips)", reason_);
        if (plainButton("Correct the stock count")) {
            act(ctx, [&]() { return ctx.svc.pharmacy.correctStock(ctx.session, m.medicineId, counted_, reason_); }, "corrected");
        }
        vspace();
        if (plainButton(m.active ? "Make unavailable" : "Make available again")) {
            act(ctx, [&]() { return ctx.svc.pharmacy.setMedicineActive(ctx.session, m.medicineId, !m.active); }, "updated");
        }
    }

    void drawAdd(Context& ctx) {
        sectionTitle("Add a medicine");
        textField("Medicine name", draft_.name);
        textField("Unit (for example tablet, ml, strip)", draft_.unit);
        textField("Opening stock (blank for 0)", draft_.initialStock);
        if (primaryButton("Add the medicine")) {
            notice_.clear();
            if (ctx.blocked(notice_)) return;
            auto r = ctx.svc.pharmacy.addMedicine(ctx.session, draft_);
            if (!r) {
                ctx.fail(notice_, r.error());
                return;
            }
            notice_.info("Added " + r.value().name + " as " + formatMedicineId(r.value().medicineId) + " with " +
                         std::to_string(r.value().stockQuantity) + " " + r.value().unit + " in stock." +
                         (r.value().lowStock ? " LOW STOCK." : ""));
            draft_ = MedicineDraft();
            load(ctx);
        }
    }

    void act(Context& ctx, const std::function<Result<Medicine>()>& call, const char* verb) {
        notice_.clear();
        if (ctx.blocked(notice_)) return;
        const Result<Medicine> r = call();
        if (!r) {
            ctx.fail(notice_, r.error());
            return;
        }
        const Medicine& m = r.value();
        notice_.info(m.name + " " + verb + ". " + std::to_string(m.stockQuantity) + " " + m.unit + " in stock" +
                     (m.active ? "" : " (unavailable)") + (m.lowStock ? ". LOW STOCK." : "."));
        const std::int64_t keep = m.medicineId;
        load(ctx);
        for (std::size_t i = 0; i < rows_.size(); ++i) {
            if (rows_[i].medicineId == keep) selected_ = static_cast<int>(i);
        }
    }

    void load(Context& ctx) {
        auto r = ctx.svc.pharmacy.listStock(ctx.session);
        selected_ = -1;
        if (!r) {
            ctx.fail(notice_, r.error());
            rows_.clear();
            return;
        }
        rows_ = r.value();
    }

    Notice notice_;
    std::vector<Medicine> rows_;
    int selected_ = -1;
    std::string editName_, editUnit_, counted_, reason_, restock_;
    MedicineDraft draft_;
};

// ---- Pharmacy: prescriptions waiting, dispense ----------------------------------------------------

void drawLineRow(const DispenseLine& l) {
    ImGui::TableSetColumnIndex(1);
    ImGui::TextUnformatted((l.patientName + " " + formatUhid(l.uhid)).c_str());
    ImGui::TableSetColumnIndex(2);
    ImGui::TextUnformatted(l.medicineName.c_str());
    ImGui::TableSetColumnIndex(3);
    ImGui::TextUnformatted(l.dosage.c_str());
    ImGui::TableSetColumnIndex(4);
    ImGui::TextUnformatted(std::to_string(l.prescribed).c_str());
    ImGui::TableSetColumnIndex(5);
    ImGui::TextUnformatted(std::to_string(l.dispensed).c_str());
    ImGui::TableSetColumnIndex(6);
    ImGui::TextUnformatted(("Dr " + l.doctorUsername).c_str());
}

class DispenseScreen final : public Screen {
public:
    void onOpen(Context& ctx) override { load(ctx); }

    void draw(Context& ctx) override {
        heading("Dispense a prescription line");
        notice_.draw();
        if (plainButton("Refresh the list")) load(ctx);
        vspace();
        if (lines_.empty()) {
            muted("Nothing is waiting to be dispensed.");
            return;
        }
        if (beginTable("lines", {"Line", "Patient", "Medicine prescribed", "Dosage", "Prescribed", "Given", "Doctor"}, tableHeight(lines_.size()))) {
            for (std::size_t i = 0; i < lines_.size(); ++i) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::PushID(static_cast<int>(i));
                if (ImGui::Selectable(formatPrescriptionId(lines_[i].itemId).c_str(), selected_ == static_cast<int>(i),
                                      ImGuiSelectableFlags_SpanAllColumns)) {
                    selected_ = static_cast<int>(i);
                    quantity_ = std::to_string(lines_[i].remaining());
                    medicine_ = -1;
                    otherOk_ = false;
                    confirming_ = false;
                }
                ImGui::PopID();
                drawLineRow(lines_[i]);
            }
            ImGui::EndTable();
        }
        if (selected_ < 0 || selected_ >= static_cast<int>(lines_.size())) return;
        drawDispense(ctx, lines_[static_cast<std::size_t>(selected_)]);
    }

private:
    void drawDispense(Context& ctx, const DispenseLine& line) {
        sectionTitle("Give the medicine");
        ImGui::TextWrapped("%s, %s: %lld still to dispense for %s (%s).", line.medicineName.c_str(), line.dosage.c_str(),
                           static_cast<long long>(line.remaining()), line.patientName.c_str(), formatUhid(line.uhid).c_str());
        vspace();
        ImGui::TextDisabled("Medicine from the stock");
        std::string preview = medicine_ >= 0 ? stock_[static_cast<std::size_t>(medicine_)].name : "Choose a medicine";
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 22.0F);
        if (ImGui::BeginCombo("##medicine", preview.c_str())) {
            for (std::size_t i = 0; i < stock_.size(); ++i) {
                const auto& m = stock_[i];
                if (!m.active) continue;
                const std::string label = m.name + "  (" + std::to_string(m.stockQuantity) + " " + m.unit + ")";
                if (ImGui::Selectable(label.c_str(), medicine_ == static_cast<int>(i))) {
                    medicine_ = static_cast<int>(i);
                    otherOk_ = false;
                    confirming_ = false;
                }
            }
            ImGui::EndCombo();
        }
        textField("Quantity to dispense", quantity_);
        if (medicine_ < 0) return;
        const Medicine& m = stock_[static_cast<std::size_t>(medicine_)];

        // Prescription names are free text, so the pharmacist may give another stock item; when the
        // name differs from what was prescribed, say so and ask again (deviation D-53).
        const bool mismatch = !sameMedicine(line.medicineName, m.name);
        if (mismatch) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.70F, 0.15F, 0.15F, 1.0F));
            ImGui::TextWrapped("The prescription says \"%s\" but you picked \"%s\".", line.medicineName.c_str(), m.name.c_str());
            ImGui::PopStyleColor();
            ImGui::Checkbox(("Give " + m.name + " for this line anyway").c_str(), &otherOk_);
        }
        vspace();
        const bool allowed = !mismatch || otherOk_;
        ImGui::BeginDisabled(!allowed);
        if (!confirming_) {
            if (primaryButton("Dispense")) {
                notice_.clear();
                auto qty = input::parseWholeNumber(text::trimSpaces(quantity_), "Quantity", 1,
                                                   ctx.svc.policy.labPharmacy.maxStockQuantity);
                if (!qty) {
                    notice_.error(qty.error());
                } else {
                    confirming_ = true;
                }
            }
        } else {
            ImGui::TextWrapped("Dispense %s x %s now?", text::trimSpaces(quantity_).c_str(), m.name.c_str());
            if (primaryButton("Yes, dispense")) dispense(ctx, line, m);
            ImGui::SameLine();
            if (plainButton("No")) confirming_ = false;
        }
        ImGui::EndDisabled();
    }

    void dispense(Context& ctx, const DispenseLine& line, const Medicine& m) {
        notice_.clear();
        confirming_ = false;
        if (ctx.blocked(notice_)) return;
        auto r = ctx.svc.pharmacy.dispense(ctx.session, line.itemId, m.medicineId, text::trimSpaces(quantity_));
        if (!r) {
            ctx.fail(notice_, r.error());
            return;
        }
        const DispenseOutcome& o = r.value();
        std::string msg = "Dispensed " + std::to_string(o.quantity) + " " + o.unit + " of " + o.medicineName + ". " +
                          (o.remainingToDispense > 0 ? std::to_string(o.remainingToDispense) + " still to dispense on this line. "
                                                     : std::string("The line is complete. ")) +
                          std::to_string(o.stockAfter) + " " + o.unit + " left in stock.";
        if (o.lowStock) msg += "\nLOW STOCK: " + o.medicineName + " is below " + std::to_string(o.threshold) + ". Please reorder.";
        load(ctx);
        notice_.info(msg);
    }

    void load(Context& ctx) {
        selected_ = -1;
        medicine_ = -1;
        auto lines = ctx.svc.pharmacy.listPendingPrescriptions(ctx.session);
        auto stock = ctx.svc.pharmacy.listStock(ctx.session);
        lines_.clear();
        stock_.clear();
        if (!lines) {
            ctx.fail(notice_, lines.error());
            return;
        }
        if (!stock) {
            ctx.fail(notice_, stock.error());
            return;
        }
        lines_ = lines.value();
        stock_ = stock.value();
    }

    Notice notice_;
    std::vector<DispenseLine> lines_;
    std::vector<Medicine> stock_;
    int selected_ = -1;
    int medicine_ = -1;
    std::string quantity_;
    bool otherOk_ = false;
    bool confirming_ = false;
};

// ---- Pharmacy: the prescriptions of one patient ---------------------------------------------------

class PatientPrescriptionsScreen final : public Screen {
public:
    void draw(Context& ctx) override {
        heading("Prescriptions of a patient");
        notice_.draw();
        if (picker_.draw(ctx)) load(ctx);
        if (!picker_.hasUhid()) return;
        if (primaryButton("Show the prescriptions")) load(ctx);
        vspace();
        if (!loaded_) return;
        if (rows_.empty()) {
            muted("No prescriptions.");
            return;
        }
        if (beginTable("rx", {"Line", "Patient", "Medicine prescribed", "Dosage", "Prescribed", "Given", "Doctor"}, tableHeight(rows_.size(), 14))) {
            for (const auto& l : rows_) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(formatPrescriptionId(l.itemId).c_str());
                drawLineRow(l);
            }
            ImGui::EndTable();
        }
    }

private:
    void load(Context& ctx) {
        loaded_ = false;
        rows_.clear();
        if (!picker_.hasUhid()) return;
        auto r = ctx.svc.pharmacy.listPrescriptionsForPatient(ctx.session, picker_.uhid());
        if (!r) {
            ctx.fail(notice_, r.error());
            return;
        }
        rows_ = r.value();
        loaded_ = true;
    }

    PatientPicker picker_{false};
    Notice notice_;
    bool loaded_ = false;
    std::vector<DispenseLine> rows_;
};

}  // namespace

std::unique_ptr<Screen> makeLabPendingScreen() { return std::make_unique<LabPendingScreen>(); }
std::unique_ptr<Screen> makeLabPatientScreen() { return std::make_unique<LabPatientScreen>(); }
std::unique_ptr<Screen> makeStockScreen() { return std::make_unique<StockScreen>(); }
std::unique_ptr<Screen> makeDispenseScreen() { return std::make_unique<DispenseScreen>(); }
std::unique_ptr<Screen> makePatientPrescriptionsScreen() { return std::make_unique<PatientPrescriptionsScreen>(); }

}  // namespace hms::gui
