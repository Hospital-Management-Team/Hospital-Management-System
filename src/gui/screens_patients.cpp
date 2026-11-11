// Patient screens: register (HMS-F-001, F-002), search (HMS-F-004) and correct details (HMS-F-005).

#include <algorithm>

#include "core/text.hpp"
#include "gui/patient_picker.hpp"
#include "gui/screens.hpp"
#include "imgui_stdlib.h"
#include "patient/patient_validation.hpp"
#include "patient/phone.hpp"
#include "patient/uhid.hpp"

namespace hms::gui {

namespace {

// The fields of the registration and correction forms, as typed.
struct PatientForm {
    std::string name;
    std::string dob;
    std::string age;
    std::string address;
    std::string phone;
    int gender = -1;  // 0 = M, 1 = F, 2 = O, -1 = not chosen
    bool noPhone = false;

    void clear() { *this = PatientForm(); }

    void load(const PatientRecord& p) {
        name = p.fullName;
        dob = p.dateOfBirth.toDisplay();
        age.clear();
        address = p.address;
        phone = p.phone.value_or("");
        gender = p.gender == Gender::Male ? 0 : (p.gender == Gender::Female ? 1 : 2);
        noPhone = !p.phone && p.noPhoneConfirmed;
    }

    PatientDraft draft() const {
        PatientDraft d;
        d.fullName = name;
        d.dateOfBirth = dob;
        d.ageYears = text::trimSpaces(dob).empty() ? age : std::string();
        d.gender = gender == 0 ? "M" : (gender == 1 ? "F" : (gender == 2 ? "O" : ""));
        d.address = address;
        d.phone = noPhone ? std::string() : phone;
        d.noPhone = noPhone;
        return d;
    }

    void draw(bool allowAge) {
        textField("* Full name", name);
        textField("* Date of birth", dob, "DD-MM-YYYY");
        if (allowAge) {
            const bool dobGiven = !text::trimSpaces(dob).empty();
            ImGui::BeginDisabled(dobGiven);
            textField("* Age in years (only if the date of birth is not known)", age);
            ImGui::EndDisabled();
        }
        ImGui::TextDisabled("* Gender");
        ImGui::RadioButton("Male", &gender, 0);
        ImGui::SameLine();
        ImGui::RadioButton("Female", &gender, 1);
        ImGui::SameLine();
        ImGui::RadioButton("Other", &gender, 2);
        textField("* Address", address);
        ImGui::BeginDisabled(noPhone);
        textField("* Mobile number", phone, "10 digits, the patient's or a family member's");
        ImGui::EndDisabled();
        ImGui::Checkbox("Neither the patient nor anyone with them has a mobile number", &noPhone);
        muted("For a child or an elderly patient, a family member's or carer's number is fine. Never enter a made-up number.");
    }
};

PatientRecord previewRecord(const NewPatient& n) {
    PatientRecord shown;
    shown.fullName = n.fullName;
    shown.dateOfBirth = n.dateOfBirth;
    shown.dobEstimated = n.dobEstimated;
    shown.gender = n.gender;
    shown.address = n.address;
    shown.phone = n.phone;
    shown.noPhoneConfirmed = n.noPhoneConfirmed;
    return shown;
}

// ---- Register a new patient ---------------------------------------------------------------

class RegisterScreen final : public Screen {
public:
    void draw(Context& ctx) override {
        heading("Register a new patient");
        notice_.draw();
        switch (step_) {
            case Step::Form: drawForm(ctx); break;
            case Step::Confirm: drawConfirm(ctx); break;
            case Step::Duplicates: drawDuplicates(ctx); break;
            case Step::Done: drawDone(ctx); break;
        }
    }

private:
    enum class Step { Form, Confirm, Duplicates, Done };

    void drawForm(Context& ctx) {
        muted("Fields marked * are required. The details are read back before anything is saved.");
        vspace();
        form_.draw(true);
        vspace();
        if (primaryButton("Review details")) {
            notice_.clear();
            if (ctx.blocked(notice_)) return;
            draft_ = form_.draft();
            auto checked = patient_rules::validatePatient(draft_, ctx.today(), ctx.svc.patients.policy().patient);
            if (!checked) {
                notice_.error(checked.error());
                return;
            }
            preview_ = previewRecord(checked.value());
            step_ = Step::Confirm;
        }
        ImGui::SameLine();
        if (plainButton("Clear the form")) {
            form_.clear();
            notice_.clear();
        }
    }

    void drawConfirm(Context& ctx) {
        sectionTitle("Please read these details back to the patient");
        patientCard(preview_, ctx.today(), false);
        vspace();
        if (primaryButton("Save patient")) {
            notice_.clear();
            if (ctx.blocked(notice_)) return;
            auto r = ctx.svc.patients.registerPatient(ctx.session, draft_, DuplicateDecision::none());
            if (!r) {
                ctx.fail(notice_, r.error());
                step_ = Step::Form;
                return;
            }
            handleOutcome(ctx, std::move(r).value());
        }
        ImGui::SameLine();
        if (plainButton("Back to edit")) step_ = Step::Form;
    }

    void handleOutcome(Context& ctx, RegistrationOutcome outcome) {
        if (outcome.status == RegistrationOutcome::Status::Registered) {
            finish(ctx, outcome.uhid, "Patient registered. UHID: " + formatUhid(outcome.uhid) +
                                          ". Write this UHID on the patient's card.");
            return;
        }
        outcome_ = std::move(outcome);
        selected_ = -1;
        reason_ = 0;
        step_ = Step::Duplicates;
    }

    void finish(Context& ctx, std::int64_t uhid, const std::string& message) {
        notice_.info(message);
        ctx.currentUhid = uhid;
        step_ = Step::Done;
    }

    void drawDuplicates(Context& ctx) {
        const auto& cands = outcome_.candidates;
        sectionTitle(cands.size() > 1 ? "Possible existing patients found" : "A possible existing patient found");
        muted("Check with the patient before continuing.");
        const float height = rowsHeight(cands.size(), 6);
        if (beginTable("candidates", {"Match", "UHID", "Name", "Date of birth", "Age", "Gender", "Mobile", "Address"}, height)) {
            for (std::size_t i = 0; i < cands.size(); ++i) {
                const auto& c = cands[i];
                const auto& p = c.patient;
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::PushID(static_cast<int>(i));
                const std::string match = (c.kind == CandidateKind::Strong ? "STRONG" : "SAME PHONE") +
                                          std::string(c.newSinceReview ? " (new)" : "");
                if (ImGui::Selectable(match.c_str(), selected_ == static_cast<int>(i), ImGuiSelectableFlags_SpanAllColumns)) {
                    selected_ = static_cast<int>(i);
                }
                ImGui::PopID();
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(formatUhid(p.uhid).c_str());
                ImGui::TableSetColumnIndex(2);
                ImGui::TextUnformatted(p.fullName.c_str());
                ImGui::TableSetColumnIndex(3);
                ImGui::TextUnformatted((p.dateOfBirth.toDisplay() + (p.dobEstimated ? "*" : "")).c_str());
                ImGui::TableSetColumnIndex(4);
                ImGui::TextUnformatted(std::to_string(ageInYears(p.dateOfBirth, ctx.today())).c_str());
                ImGui::TableSetColumnIndex(5);
                ImGui::TextUnformatted(to_string(p.gender).c_str());
                ImGui::TableSetColumnIndex(6);
                ImGui::TextUnformatted(phoneCell(p).c_str());
                ImGui::TableSetColumnIndex(7);
                ImGui::TextUnformatted(p.address.c_str());
            }
            ImGui::EndTable();
        }
        muted("STRONG = same name, date of birth and gender.  SAME PHONE = shares this mobile number.  * = estimated from age.");
        vspace();

        std::vector<std::int64_t> reviewed;
        for (const auto& c : cands) reviewed.push_back(c.patient.uhid);

        ImGui::BeginDisabled(selected_ < 0);
        if (primaryButton("Same patient: use the selected UHID")) {
            notice_.clear();
            if (!ctx.blocked(notice_)) {
                const std::int64_t uhid = cands[static_cast<std::size_t>(selected_)].patient.uhid;
                auto r = ctx.svc.patients.useExistingPatient(ctx.session, draft_, uhid, reviewed);
                if (!r) {
                    ctx.fail(notice_, r.error());
                } else {
                    finish(ctx, uhid, "No new record was created. Use the existing UHID " + formatUhid(uhid) + ".");
                }
            }
        }
        ImGui::EndDisabled();

        if (outcome_.differentPersonAllowed) {
            sectionTitle("Different person");
            if (outcome_.reasonRequired) {
                ImGui::TextDisabled("Why is this a different person?");
                ImGui::RadioButton(reasonLabel(DuplicateReason::TwinsOrMultipleBirth).c_str(), &reason_, 0);
                ImGui::RadioButton(reasonLabel(DuplicateReason::DifferentPersonSameNameAndDob).c_str(), &reason_, 1);
                ImGui::RadioButton(reasonLabel(DuplicateReason::Other).c_str(), &reason_, 2);
            }
            if (plainButton("Different person: create a new UHID")) {
                notice_.clear();
                if (!ctx.blocked(notice_)) {
                    DuplicateDecision decision;
                    decision.kind = DuplicateDecision::Kind::DifferentPerson;
                    decision.reviewedUhids = reviewed;
                    if (outcome_.reasonRequired) {
                        decision.reason = reason_ == 0   ? DuplicateReason::TwinsOrMultipleBirth
                                          : reason_ == 1 ? DuplicateReason::DifferentPersonSameNameAndDob
                                                         : DuplicateReason::Other;
                    }
                    auto r = ctx.svc.patients.registerPatient(ctx.session, draft_, decision);
                    if (!r) {
                        ctx.fail(notice_, r.error());
                    } else if (r.value().status == RegistrationOutcome::Status::Registered) {
                        finish(ctx, r.value().uhid, "Patient registered as a different person. UHID: " +
                                                        formatUhid(r.value().uhid) + ". Write this UHID on the patient's card.");
                    } else {
                        notice_.error("Another possible match was registered while you were deciding. Please review again.");
                        outcome_ = std::move(r).value();
                        selected_ = -1;
                    }
                }
            }
        } else {
            muted("Hospital policy: a patient sharing this mobile number must use the existing UHID.");
        }

        vspace();
        if (plainButton("Cancel (nothing is saved)")) {
            notice_.info("Cancelled. Nothing was saved.");
            step_ = Step::Form;
        }
    }

    void drawDone(Context& ctx) {
        if (primaryButton("Open this patient")) ctx.goTo = "Patients";
        ImGui::SameLine();
        if (plainButton("Register another patient")) {
            form_.clear();
            notice_.clear();
            step_ = Step::Form;
        }
    }

    Step step_ = Step::Form;
    PatientForm form_;
    PatientDraft draft_;
    PatientRecord preview_;
    RegistrationOutcome outcome_;
    int selected_ = -1;
    int reason_ = 0;
    Notice notice_;
};

// ---- Search patients and correct their details --------------------------------------------

class PatientsScreen final : public Screen {
public:
    void draw(Context& ctx) override {
        heading("Patients");
        followCurrent(ctx);
        drawSearch(ctx);
        notice_.draw();
        if (!rows_.empty()) drawResults(ctx);
        if (selected_ >= 0 && selected_ < static_cast<int>(rows_.size())) drawDetail(ctx);
    }

private:
    enum class Edit { None, Form, Confirm };

    // A patient picked elsewhere (for example just registered) is shown here as the result.
    void followCurrent(Context& ctx) {
        if (ctx.currentUhid == 0 || ctx.currentUhid == followed_) return;
        followed_ = ctx.currentUhid;
        auto r = ctx.svc.patients.getPatient(ctx.session, ctx.currentUhid);
        if (!r) return;
        rows_.assign(1, r.value());
        total_ = 1;
        selected_ = 0;
        edit_ = Edit::None;
        notice_.clear();
    }

    void drawSearch(Context& ctx) {
        ImGui::TextDisabled("Search by");
        ImGui::RadioButton("UHID", &mode_, 0);
        ImGui::SameLine();
        ImGui::RadioButton("Name", &mode_, 1);
        ImGui::SameLine();
        ImGui::RadioButton("Mobile number", &mode_, 2);
        ImGui::SameLine();
        ImGui::RadioButton("Name and date of birth", &mode_, 3);

        bool go = false;
        if (mode_ == 3) {
            go = textField("Any part of the name", name_, nullptr, ImGuiInputTextFlags_EnterReturnsTrue) || go;
            dateField("Date of birth", dob_, ctx.today());
        } else {
            const char* labels[3] = {"UHID (for example HMS-000123, or 123)", "Any part of the name (case does not matter)",
                                     "Mobile number, in any format"};
            go = textField(labels[mode_], query_, nullptr, ImGuiInputTextFlags_EnterReturnsTrue) || go;
        }
        if (primaryButton("Search")) go = true;
        ImGui::SameLine();
        if (plainButton("Clear")) {
            query_.clear();
            name_.clear();
            dob_.clear();
            rows_.clear();
            selected_ = -1;
            notice_.clear();
            edit_ = Edit::None;
        }
        if (go) runSearch(ctx);
    }

    void runSearch(Context& ctx) {
        notice_.clear();
        edit_ = Edit::None;
        selected_ = -1;
        rows_.clear();
        Result<SearchResult> r = mode_ == 3
                                     ? ctx.svc.patients.searchByNameAndDob(ctx.session, NameDobQuery{name_, dob_})
                                     : ctx.svc.patients.searchPatients(
                                           ctx.session, mode_ == 0 ? SearchMode::Uhid : (mode_ == 1 ? SearchMode::Name : SearchMode::Phone),
                                           query_);
        if (!r) {
            ctx.fail(notice_, r.error());
            return;
        }
        rows_ = r.value().rows;
        total_ = r.value().totalMatches;
        if (rows_.empty()) {
            notice_.error("No patient found.");
            return;
        }
        std::string msg = std::to_string(total_) + (total_ == 1 ? " patient found." : " patients found.");
        if (r.value().truncated()) msg += " Only the first " + std::to_string(rows_.size()) + " are shown; narrow the search.";
        notice_.info(msg);
        if (rows_.size() == 1) selectRow(ctx, 0);
    }

    void selectRow(Context& ctx, int index) {
        selected_ = index;
        edit_ = Edit::None;
        ctx.currentUhid = rows_[static_cast<std::size_t>(index)].uhid;
        followed_ = ctx.currentUhid;
    }

    void drawResults(Context& ctx) {
        const float height = rowsHeight(rows_.size(), 8);
        if (!beginTable("results", {"UHID", "Name", "Date of birth", "Age", "Gender", "Mobile"}, height)) return;
        for (std::size_t i = 0; i < rows_.size(); ++i) {
            const auto& p = rows_[i];
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::PushID(static_cast<int>(i));
            if (ImGui::Selectable(formatUhid(p.uhid).c_str(), selected_ == static_cast<int>(i), ImGuiSelectableFlags_SpanAllColumns)) {
                selectRow(ctx, static_cast<int>(i));
            }
            ImGui::PopID();
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(p.fullName.c_str());
            ImGui::TableSetColumnIndex(2);
            ImGui::TextUnformatted((p.dateOfBirth.toDisplay() + (p.dobEstimated ? "*" : "")).c_str());
            ImGui::TableSetColumnIndex(3);
            ImGui::TextUnformatted(std::to_string(ageInYears(p.dateOfBirth, ctx.today())).c_str());
            ImGui::TableSetColumnIndex(4);
            ImGui::TextUnformatted(to_string(p.gender).c_str());
            ImGui::TableSetColumnIndex(5);
            ImGui::TextUnformatted(phoneCell(p).c_str());
        }
        ImGui::EndTable();
    }

    void drawDetail(Context& ctx) {
        PatientRecord& p = rows_[static_cast<std::size_t>(selected_)];
        sectionTitle(("Patient " + formatUhid(p.uhid)).c_str());
        if (edit_ == Edit::None) {
            patientCard(p, ctx.today());
            if (ctx.role == auth::Role::Receptionist) {
                if (primaryButton("Book an appointment")) ctx.goTo = "Book appointment";
                ImGui::SameLine();
                if (plainButton("Correct these details")) {
                    form_.load(p);
                    notice_.clear();
                    edit_ = Edit::Form;
                }
            }
            return;
        }
        if (edit_ == Edit::Form) {
            muted("Change what is wrong and leave the rest as it is.");
            vspace();
            form_.draw(false);
            vspace();
            if (primaryButton("Review changes")) {
                notice_.clear();
                if (ctx.blocked(notice_)) return;
                draft_ = form_.draft();
                auto checked = patient_rules::validatePatient(draft_, ctx.today(), ctx.svc.patients.policy().patient);
                if (!checked) {
                    notice_.error(checked.error());
                    return;
                }
                preview_ = previewRecord(checked.value());
                preview_.uhid = p.uhid;
                preview_.dobEstimated = (p.dobEstimated && preview_.dateOfBirth == p.dateOfBirth) || preview_.dobEstimated;
                edit_ = Edit::Confirm;
            }
            ImGui::SameLine();
            if (plainButton("Cancel")) edit_ = Edit::None;
            return;
        }
        ImGui::TextUnformatted("The record will read:");
        patientCard(preview_, ctx.today());
        vspace();
        if (primaryButton("Save these details")) {
            notice_.clear();
            if (ctx.blocked(notice_)) return;
            auto r = ctx.svc.patients.updateDemographics(ctx.session, p.uhid, draft_);
            if (!r) {
                ctx.fail(notice_, r.error());
                edit_ = Edit::Form;
                return;
            }
            if (!r.value().changed()) {
                notice_.info("Nothing was changed: the details are the same as before.");
            } else {
                std::string fields;
                for (const auto& f : r.value().changedFields) fields += (fields.empty() ? "" : ", ") + f;
                notice_.info("Details saved for " + formatUhid(p.uhid) + ". Changed: " + fields + ".");
                p = r.value().record;
            }
            edit_ = Edit::None;
        }
        ImGui::SameLine();
        if (plainButton("Back to edit")) edit_ = Edit::Form;
    }

    int mode_ = 1;
    std::string query_;
    std::string name_;
    std::string dob_;
    std::vector<PatientRecord> rows_;
    std::size_t total_ = 0;
    int selected_ = -1;
    std::int64_t followed_ = 0;
    Edit edit_ = Edit::None;
    PatientForm form_;
    PatientDraft draft_;
    PatientRecord preview_;
    Notice notice_;
};

}  // namespace

std::unique_ptr<Screen> makeRegisterScreen() { return std::make_unique<RegisterScreen>(); }
std::unique_ptr<Screen> makePatientsScreen() { return std::make_unique<PatientsScreen>(); }

}  // namespace hms::gui
