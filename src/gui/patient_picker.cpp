#include "gui/patient_picker.hpp"

#include "imgui_stdlib.h"
#include "patient/uhid.hpp"

namespace hms::gui {

void PatientPicker::clear() {
    text_.clear();
    patient_.reset();
    uhid_ = 0;
    notice_.clear();
}

bool PatientPicker::lookUp(Context& ctx, const std::string& text) {
    notice_.clear();
    auto id = parseUhid(text);
    if (!id) {
        patient_.reset();
        uhid_ = 0;
        notice_.error("UHID: must look like HMS-000123 or 123");
        return false;
    }
    if (!fetchRecord_) {
        patient_.reset();
        uhid_ = *id;
        text_ = formatUhid(uhid_);
        ctx.currentUhid = uhid_;
        followed_ = ctx.currentUhid;
        return true;
    }
    auto r = ctx.svc.patients.getPatient(ctx.session, *id);
    if (!r) {
        patient_.reset();
        uhid_ = 0;
        ctx.fail(notice_, r.error());
        return false;
    }
    patient_ = r.value();
    uhid_ = patient_->uhid;
    text_ = formatUhid(patient_->uhid);
    ctx.currentUhid = patient_->uhid;
    followed_ = ctx.currentUhid;
    return true;
}

bool PatientPicker::draw(Context& ctx, const char* label) {
    bool changed = false;

    // Another screen found a patient: follow it.
    if (ctx.currentUhid != 0 && ctx.currentUhid != followed_) {
        followed_ = ctx.currentUhid;
        changed = lookUp(ctx, formatUhid(ctx.currentUhid)) || changed;
    }

    ImGui::PushID(this);
    ImGui::TextDisabled("%s", label);
    const float buttonWidth = ImGui::CalcTextSize("Look up").x + ImGui::GetStyle().FramePadding.x * 2.0F;
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - buttonWidth - ImGui::GetStyle().ItemSpacing.x);
    bool go = ImGui::InputText("##uhid", &text_, ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::SameLine();
    go = ImGui::Button("Look up") || go;
    ImGui::PopID();

    if (go) changed = lookUp(ctx, text_) || changed;

    notice_.draw();
    if (patient_) {
        ImGui::TextWrapped("%s (%s), born %s", patient_->fullName.c_str(), formatUhid(patient_->uhid).c_str(),
                           dobText(*patient_, ctx.today()).c_str());
    }
    if (!patient_ && uhid_ != 0) ImGui::TextWrapped("%s", formatUhid(uhid_).c_str());
    return changed;
}

}  // namespace hms::gui
