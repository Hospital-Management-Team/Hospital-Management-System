// Doctor screens: appointments (HMS-F-021), record a consultation (HMS-F-006), past consultations,
// medical history (HMS-F-003), lab results of the doctor's own patients, and the doctor's weekly hours.

#include <algorithm>

#include "appointment/appointment.hpp"
#include "billing/receipt.hpp"
#include "core/text.hpp"
#include "gui/patient_picker.hpp"
#include "gui/screens.hpp"
#include "imgui_stdlib.h"
#include "patient/uhid.hpp"
#include "validation/input.hpp"

namespace hms::gui {

namespace {

std::string orNone(const std::string& s) { return s.empty() ? "(none recorded)" : s; }

float tableHeight(std::size_t rows, std::size_t most = 12) { return rowsHeight(rows, most); }

// ---- My appointments ------------------------------------------------------------------------

class MyAppointmentsScreen final : public Screen {
public:
    void onOpen(Context& ctx) override { date_ = ctx.today().toDisplay(); }

    void draw(Context& ctx) override {
        if (date_.empty()) date_ = ctx.today().toDisplay();
        heading("My appointments");
        notice_.draw();
        ImGui::RadioButton("One day", &mode_, 0);
        ImGui::SameLine();
        ImGui::RadioButton("The week from that day", &mode_, 1);
        dateField("Date", date_, ctx.today());
        if (primaryButton("Show my appointments")) load(ctx);
        if (!loaded_) return;

        sectionTitle(title_.c_str());
        if (rows_.empty()) {
            muted("No appointments.");
            return;
        }
        if (beginTable("queue", {"When", "Appointment", "UHID", "Patient", "Status"}, tableHeight(rows_.size()))) {
            for (std::size_t i = 0; i < rows_.size(); ++i) {
                const auto& a = rows_[i];
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::PushID(static_cast<int>(i));
                const std::string when = mode_ == 1 ? dayText(a.start.date) + " " + a.start.timeText() : a.start.timeText();
                if (ImGui::Selectable(when.c_str(), selected_ == static_cast<int>(i), ImGuiSelectableFlags_SpanAllColumns)) {
                    selected_ = static_cast<int>(i);
                }
                ImGui::PopID();
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(formatAppointmentId(a.id).c_str());
                ImGui::TableSetColumnIndex(2);
                ImGui::TextUnformatted(formatUhid(a.uhid).c_str());
                ImGui::TableSetColumnIndex(3);
                ImGui::TextUnformatted(a.patientName.c_str());
                ImGui::TableSetColumnIndex(4);
                ImGui::TextUnformatted(to_string(a.status).c_str());
            }
            ImGui::EndTable();
        }
        if (selected_ < 0 || selected_ >= static_cast<int>(rows_.size())) return;
        const auto& a = rows_[static_cast<std::size_t>(selected_)];
        vspace();
        if (plainButton("Open the patient's details")) {
            ctx.currentUhid = a.uhid;
            ctx.goTo = "Patients";
        }
        ImGui::SameLine();
        if (primaryButton("Record the consultation")) {
            ctx.currentUhid = a.uhid;
            ctx.appointmentForNext = a.id;
            ctx.goTo = "Record consultation";
        }
    }

private:
    void load(Context& ctx) {
        notice_.clear();
        loaded_ = false;
        selected_ = -1;
        auto date = input::parseDate(date_, "Date");
        if (!date) {
            notice_.error(date.error());
            return;
        }
        auto r = mode_ == 1 ? ctx.svc.appointments.myWeekQueue(ctx.session, date.value())
                            : ctx.svc.appointments.myQueue(ctx.session, date.value());
        if (!r) {
            ctx.fail(notice_, r.error());
            return;
        }
        rows_ = r.value();
        title_ = (mode_ == 1 ? "Week from " : "") + dayText(date.value()) + ": " + std::to_string(rows_.size()) +
                 (rows_.size() == 1 ? " appointment" : " appointments");
        loaded_ = true;
    }

    int mode_ = 0;
    std::string date_;
    std::string title_;
    std::vector<AppointmentRecord> rows_;
    int selected_ = -1;
    bool loaded_ = false;
    Notice notice_;
};

// ---- Record a consultation ------------------------------------------------------------------

class RecordConsultationScreen final : public Screen {
public:
    void onOpen(Context& ctx) override {
        if (ctx.appointmentForNext != 0) {
            appointmentText_ = formatAppointmentId(ctx.appointmentForNext);
            ctx.appointmentForNext = 0;
            step_ = Step::Pick;
            notice_.clear();
            check(ctx);
        }
    }

    void draw(Context& ctx) override {
        heading("Record a consultation");
        notice_.draw();
        switch (step_) {
            case Step::Pick: drawPick(ctx); break;
            case Step::Form: drawForm(ctx); break;
            case Step::Saved: drawSaved(); break;
        }
    }

private:
    enum class Step { Pick, Form, Saved };

    void drawPick(Context& ctx) {
        muted("Enter the appointment number. It can be recorded once the appointment has started.");
        vspace();
        const bool go = textField("Appointment number (for example APT-000012)", appointmentText_, nullptr,
                                  ImGuiInputTextFlags_EnterReturnsTrue);
        if (primaryButton("Continue") || go) check(ctx);
    }

    void check(Context& ctx) {
        notice_.clear();
        auto id = parseAppointmentId(appointmentText_);
        if (!id) {
            notice_.error("Appointment: must look like APT-000012 or 12");
            return;
        }
        // Refuse right away if this appointment cannot be recorded, before the form is filled in.
        auto r = ctx.svc.consultations.checkRecordable(ctx.session, *id);
        if (!r) {
            ctx.fail(notice_, r.error());
            return;
        }
        draft_ = ConsultationDraft();
        draft_.appointmentId = *id;
        step_ = Step::Form;
    }

    void drawForm(Context& ctx) {
        const auto& cp = ctx.svc.policy.consultation;
        ImGui::TextUnformatted(("Appointment " + formatAppointmentId(draft_.appointmentId)).c_str());
        vspace();
        multilineField("* Diagnosis", draft_.diagnosis, 3.0F);

        sectionTitle("Medicines");
        for (std::size_t i = 0; i < draft_.prescriptions.size(); ++i) {
            auto& p = draft_.prescriptions[i];
            ImGui::PushID(static_cast<int>(i));
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14.0F);
            ImGui::InputTextWithHint("##name", "Medicine", &p.medicineName);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 18.0F);
            ImGui::InputTextWithHint("##dosage", "Dosage, e.g. 1 tablet twice a day", &p.dosage);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 5.0F);
            ImGui::InputTextWithHint("##qty", "Total", &p.quantity);
            ImGui::SameLine();
            if (plainButton("Remove")) {
                draft_.prescriptions.erase(draft_.prescriptions.begin() + static_cast<std::ptrdiff_t>(i));
                ImGui::PopID();
                break;
            }
            ImGui::PopID();
        }
        if (draft_.prescriptions.size() < cp.maxItemsPerConsultation && plainButton("Add a medicine")) {
            draft_.prescriptions.emplace_back();
        }

        sectionTitle("Laboratory tests");
        for (std::size_t i = 0; i < draft_.labOrders.size(); ++i) {
            auto& l = draft_.labOrders[i];
            ImGui::PushID(1000 + static_cast<int>(i));
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14.0F);
            ImGui::InputTextWithHint("##test", "Test", &l.testName);
            ImGui::SameLine();
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 24.0F);
            ImGui::InputTextWithHint("##instructions", "Instructions (optional)", &l.instructions);
            ImGui::SameLine();
            if (plainButton("Remove")) {
                draft_.labOrders.erase(draft_.labOrders.begin() + static_cast<std::ptrdiff_t>(i));
                ImGui::PopID();
                break;
            }
            ImGui::PopID();
        }
        if (draft_.labOrders.size() < cp.maxItemsPerConsultation && plainButton("Add a test")) {
            draft_.labOrders.emplace_back();
        }

        vspace(2.0F);
        if (primaryButton("Save the consultation")) {
            notice_.clear();
            if (ctx.blocked(notice_)) return;
            auto r = ctx.svc.consultations.recordConsultation(ctx.session, draft_);
            if (!r) {
                ctx.fail(notice_, r.error());
                return;
            }
            saved_ = r.value();
            notice_.info("Consultation " + formatConsultationId(saved_.consultationId) + " saved for " + saved_.patientName +
                         " (" + formatUhid(saved_.uhid) + ").");
            step_ = Step::Saved;
        }
        ImGui::SameLine();
        if (plainButton("Cancel")) {
            step_ = Step::Pick;
            notice_.info("Cancelled. Nothing was saved.");
        }
    }

    void drawSaved() {
        keyValue("Diagnosis", saved_.diagnosis);
        for (const auto& p : saved_.prescriptions) {
            keyValue("Medicine", p.medicineName + ", " + p.dosage + ", total " + std::to_string(p.quantity));
        }
        for (const auto& l : saved_.labOrders) {
            keyValue("Test", l.testName + (l.instructions ? " (" + *l.instructions + ")" : std::string()));
        }
        vspace();
        if (plainButton("Record another consultation")) {
            appointmentText_.clear();
            notice_.clear();
            step_ = Step::Pick;
        }
    }

    Step step_ = Step::Pick;
    std::string appointmentText_;
    ConsultationDraft draft_;
    ConsultationRecord saved_;
    Notice notice_;
};

// ---- Past consultations of a patient --------------------------------------------------------

void drawConsultation(const ConsultationRecord& c) {
    const std::string header = formatConsultationId(c.consultationId) + "   " + c.slot.toDisplay() + "   Dr " + c.doctorUsername +
                               "   " + formatAppointmentId(c.appointmentId) + "###c" + std::to_string(c.consultationId);
    if (!ImGui::CollapsingHeader(header.c_str())) return;
    keyValue("Diagnosis", c.diagnosis);
    if (c.prescriptions.empty()) keyValue("Medicines", "none");
    for (const auto& p : c.prescriptions) {
        keyValue("Medicine", p.medicineName + ", " + p.dosage + ", total " + std::to_string(p.quantity));
    }
    if (c.labOrders.empty()) keyValue("Tests", "none");
    for (const auto& l : c.labOrders) {
        keyValue("Test", l.testName + (l.instructions ? " (" + *l.instructions + ")" : std::string()));
    }
}

class PastConsultationsScreen final : public Screen {
public:
    void draw(Context& ctx) override {
        heading("Past consultations");
        muted("Only a doctor with a booked or completed appointment with the patient can open these.");
        vspace();
        notice_.draw();
        if (picker_.draw(ctx)) load(ctx);
        if (!picker_.patient()) return;
        if (primaryButton("Show the consultations")) load(ctx);
        vspace();
        if (!loaded_) return;
        if (list_.empty()) {
            muted("No consultations recorded for this patient.");
            return;
        }
        for (const auto& c : list_) drawConsultation(c);
    }

private:
    void load(Context& ctx) {
        notice_.clear();
        loaded_ = false;
        list_.clear();
        if (!picker_.patient()) return;
        auto r = ctx.svc.consultations.listPatientHistory(ctx.session, picker_.uhid());
        if (!r) {
            ctx.fail(notice_, r.error());
            return;
        }
        list_ = r.value();
        loaded_ = true;
    }

    PatientPicker picker_;
    Notice notice_;
    bool loaded_ = false;
    std::vector<ConsultationRecord> list_;
};

// ---- Medical history ------------------------------------------------------------------------

class MedicalHistoryScreen final : public Screen {
public:
    void draw(Context& ctx) override {
        heading("Medical history, allergies and current medications");
        muted("Opening a history is audited. Only a doctor with a booked or completed appointment with the patient can open it.");
        vspace();
        notice_.draw();
        if (picker_.draw(ctx)) {
            editing_ = false;
            history_ = MedicalHistory();
            loaded_ = false;
        }
        if (!picker_.patient()) return;
        if (!loaded_ && primaryButton("Open the medical history")) load(ctx);
        if (!loaded_) return;

        const auto offset = ctx.svc.records.policy().utcOffsetMinutes;
        sectionTitle(("Medical history of " + history_.patientName).c_str());
        if (const auto* current = history_.current()) {
            ImGui::TextDisabled("%s recorded %s by Dr %s", formatHistoryVersionId(current->versionId).c_str(),
                                localDateTimeText(current->recordedAt, offset).c_str(), current->authorUsername.c_str());
            keyValue("History", orNone(current->history));
            keyValue("Allergies", orNone(current->allergies));
            keyValue("Medications", orNone(current->currentMedications));
        } else {
            muted("No medical history recorded.");
        }
        vspace();
        if (!editing_) {
            if (primaryButton("Update (saves a new version)")) {
                draft_ = MedicalHistoryDraft();
                if (const auto* current = history_.current()) {
                    draft_ = MedicalHistoryDraft{current->history, current->allergies, current->currentMedications};
                }
                editing_ = true;
            }
        } else {
            multilineField("Medical history", draft_.history, 4.0F);
            multilineField("Allergies", draft_.allergies, 3.0F);
            multilineField("Current medications (maintained by the doctor)", draft_.currentMedications, 3.0F);
            if (primaryButton("Save as a new version")) save(ctx);
            ImGui::SameLine();
            if (plainButton("Cancel")) editing_ = false;
        }
        if (history_.versions.size() > 1) {
            sectionTitle("Earlier versions (newest first)");
            for (std::size_t i = 1; i < history_.versions.size(); ++i) {
                const auto& v = history_.versions[i];
                const std::string header = formatHistoryVersionId(v.versionId) + "   " + localDateTimeText(v.recordedAt, offset) +
                                           "   Dr " + v.authorUsername + "###v" + std::to_string(v.versionId);
                if (!ImGui::CollapsingHeader(header.c_str())) continue;
                keyValue("History", orNone(v.history));
                keyValue("Allergies", orNone(v.allergies));
                keyValue("Medications", orNone(v.currentMedications));
            }
        }
    }

private:
    void load(Context& ctx) {
        notice_.clear();
        auto r = ctx.svc.records.openHistory(ctx.session, picker_.uhid());
        if (!r) {
            ctx.fail(notice_, r.error());
            return;
        }
        history_ = r.value();
        loaded_ = true;
    }

    void save(Context& ctx) {
        notice_.clear();
        if (ctx.blocked(notice_)) return;
        auto r = ctx.svc.records.updateHistory(ctx.session, picker_.uhid(), draft_);
        if (!r) {
            ctx.fail(notice_, r.error());
            return;
        }
        notice_.info("Saved as " + formatHistoryVersionId(r.value().versionId) + ". The earlier versions are kept unchanged.");
        editing_ = false;
        load(ctx);
    }

    PatientPicker picker_;
    Notice notice_;
    MedicalHistory history_;
    MedicalHistoryDraft draft_;
    bool loaded_ = false;
    bool editing_ = false;
};

// ---- Lab results of the doctor's patients ---------------------------------------------------

class LabResultsScreen final : public Screen {
public:
    void draw(Context& ctx) override {
        heading("Lab results");
        muted("The results of the tests you ordered for the patient.");
        vspace();
        notice_.draw();
        if (picker_.draw(ctx)) load(ctx);
        if (!picker_.patient()) return;
        if (primaryButton("Show the results")) load(ctx);
        vspace();
        if (!loaded_) return;
        if (list_.empty()) {
            muted("None of your tests for this patient has a result yet.");
            return;
        }
        const int offset = ctx.svc.lab.policy().utcOffsetMinutes;
        for (const auto& o : list_) {
            const std::string header = formatLabOrderId(o.orderId) + "   " + o.testName + "   (revision " +
                                       std::to_string(o.latest ? o.latest->revision : 0) + ")###l" + std::to_string(o.orderId);
            if (!ImGui::CollapsingHeader(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen)) continue;
            keyValue("Result", o.latest ? o.latest->text : std::string("(none)"));
            if (o.latest) {
                keyValue("Entered", localDateTimeText(o.latest->enteredAt, offset) + " by " + o.latest->enteredBy);
            }
        }
    }

private:
    void load(Context& ctx) {
        notice_.clear();
        loaded_ = false;
        list_.clear();
        if (!picker_.patient()) return;
        auto r = ctx.svc.lab.listResultsForPatient(ctx.session, picker_.uhid());
        if (!r) {
            ctx.fail(notice_, r.error());
            return;
        }
        list_ = r.value();
        loaded_ = true;
    }

    PatientPicker picker_;
    Notice notice_;
    bool loaded_ = false;
    std::vector<LabOrderView> list_;
};

// ---- My weekly hours ------------------------------------------------------------------------

class MyHoursScreen final : public Screen {
public:
    void onOpen(Context& ctx) override { load(ctx); }

    void draw(Context& ctx) override {
        heading("My weekly working hours");
        notice_.draw();
        if (!loaded_) {
            if (primaryButton("Load my hours")) load(ctx);
            return;
        }
        ImGui::TextWrapped("Department: %s", profile_.hasProfile ? profile_.department.c_str() : "(not set by the Administrator yet)");
        muted("One window per weekday, typed as 09:00-13:00, or off. Booked appointments outside new hours are kept.");
        vspace();
        for (int day = 1; day <= 7; ++day) {
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14.0F);
            ImGui::InputTextWithHint((weekdayName(day) + "##d" + std::to_string(day)).c_str(), "09:00-13:00 or off",
                                     &text_[static_cast<std::size_t>(day - 1)]);
        }
        vspace();
        if (primaryButton("Save my hours")) save(ctx);
    }

private:
    void load(Context& ctx) {
        notice_.clear();
        auto r = ctx.svc.appointments.myProfile(ctx.session);
        if (!r) {
            ctx.fail(notice_, r.error());
            return;
        }
        profile_ = r.value();
        for (int day = 1; day <= 7; ++day) text_[static_cast<std::size_t>(day - 1)] = hoursText(profile_, day);
        loaded_ = true;
    }

    void save(Context& ctx) {
        notice_.clear();
        if (ctx.blocked(notice_)) return;
        std::vector<WeeklyHours> hours;
        for (int day = 1; day <= 7; ++day) {
            auto h = parseHoursText(text_[static_cast<std::size_t>(day - 1)], day);
            if (!h) {
                notice_.error(h.error());
                return;
            }
            if (h.value()) hours.push_back(*h.value());
        }
        auto r = ctx.svc.appointments.setOwnHours(ctx.session, hours);
        if (!r) {
            ctx.fail(notice_, r.error());
            return;
        }
        std::string msg = "Saved. The booking screen uses these hours.";
        if (r.value().bookedOutsideHours > 0) {
            msg += " " + std::to_string(r.value().bookedOutsideHours) + " booked appointment(s) fall outside the new hours and are kept.";
        }
        notice_.info(msg);
        load(ctx);
        notice_.info(msg);
    }

    Notice notice_;
    bool loaded_ = false;
    DoctorSummary profile_;
    std::string text_[7];
};

}  // namespace

std::unique_ptr<Screen> makeMyAppointmentsScreen() { return std::make_unique<MyAppointmentsScreen>(); }
std::unique_ptr<Screen> makeRecordConsultationScreen() { return std::make_unique<RecordConsultationScreen>(); }
std::unique_ptr<Screen> makePastConsultationsScreen() { return std::make_unique<PastConsultationsScreen>(); }
std::unique_ptr<Screen> makeMedicalHistoryScreen() { return std::make_unique<MedicalHistoryScreen>(); }
std::unique_ptr<Screen> makeLabResultsScreen() { return std::make_unique<LabResultsScreen>(); }
std::unique_ptr<Screen> makeMyHoursScreen() { return std::make_unique<MyHoursScreen>(); }

}  // namespace hms::gui
