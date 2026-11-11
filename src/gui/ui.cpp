#include "gui/ui.hpp"

#include <algorithm>
#include <cfloat>
#include <utility>

#include "appointment/appointment.hpp"
#include "core/money.hpp"
#include "core/text.hpp"
#include "imgui_stdlib.h"
#include "patient/phone.hpp"
#include "patient/uhid.hpp"
#include "validation/input.hpp"

namespace hms::gui {

namespace {

constexpr ImVec4 kAccent{0.05F, 0.50F, 0.58F, 1.0F};
constexpr ImVec4 kAccentHover{0.07F, 0.58F, 0.67F, 1.0F};
constexpr ImVec4 kAccentDown{0.04F, 0.42F, 0.49F, 1.0F};
constexpr ImVec4 kDanger{0.78F, 0.20F, 0.20F, 1.0F};
constexpr ImVec4 kDangerHover{0.86F, 0.27F, 0.27F, 1.0F};
constexpr ImVec4 kDangerDown{0.66F, 0.15F, 0.15F, 1.0F};

bool coloredButton(const char* label, float width, const ImVec4& base, const ImVec4& hover, const ImVec4& down) {
    ImGui::PushStyleColor(ImGuiCol_Button, base);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, hover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, down);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0F, 1.0F, 1.0F, 1.0F));
    const bool pressed = ImGui::Button(label, ImVec2(width, 0.0F));
    ImGui::PopStyleColor(4);
    return pressed;
}

}  // namespace

// ---- Notice -----------------------------------------------------------------------------

void Notice::info(std::string text) {
    kind_ = Kind::Info;
    text_ = std::move(text);
}

void Notice::error(const Error& e) {
    kind_ = Kind::Error;
    text_ = e.describe();
}

void Notice::error(std::string text) {
    kind_ = Kind::Error;
    text_ = std::move(text);
}

void Notice::clear() {
    kind_ = Kind::None;
    text_.clear();
}

void Notice::draw() const {
    if (kind_ == Kind::None) return;
    const bool isError = kind_ == Kind::Error;
    ImGui::PushStyleColor(ImGuiCol_ChildBg, isError ? ImVec4(0.99F, 0.92F, 0.92F, 1.0F) : ImVec4(0.90F, 0.97F, 0.92F, 1.0F));
    ImGui::PushStyleColor(ImGuiCol_Border, isError ? ImVec4(0.90F, 0.55F, 0.55F, 1.0F) : ImVec4(0.55F, 0.80F, 0.62F, 1.0F));
    ImGui::PushStyleColor(ImGuiCol_Text, isError ? ImVec4(0.55F, 0.10F, 0.10F, 1.0F) : ImVec4(0.08F, 0.38F, 0.18F, 1.0F));
    ImGui::PushID(this);
    if (ImGui::BeginChild("notice", ImVec2(0.0F, 0.0F), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY)) {
        ImGui::TextWrapped("%s", text_.c_str());
    }
    ImGui::EndChild();
    ImGui::PopID();
    ImGui::PopStyleColor(3);
    ImGui::Spacing();
}

// ---- Look -------------------------------------------------------------------------------

void applyTheme(float scale) {
    ImGuiStyle& s = ImGui::GetStyle();
    s = ImGuiStyle();
    ImGui::StyleColorsLight();
    s.WindowRounding = 0.0F;
    s.ChildRounding = 6.0F;
    s.FrameRounding = 5.0F;
    s.PopupRounding = 6.0F;
    s.GrabRounding = 5.0F;
    s.TabRounding = 5.0F;
    s.FramePadding = ImVec2(10.0F, 7.0F);
    s.ItemSpacing = ImVec2(10.0F, 9.0F);
    s.WindowPadding = ImVec2(16.0F, 14.0F);
    s.CellPadding = ImVec2(8.0F, 6.0F);
    s.ScrollbarSize = 14.0F;
    s.FrameBorderSize = 1.0F;
    s.ScaleAllSizes(scale);

    ImVec4* c = s.Colors;
    c[ImGuiCol_Text] = ImVec4(0.11F, 0.13F, 0.16F, 1.0F);
    c[ImGuiCol_TextDisabled] = ImVec4(0.43F, 0.47F, 0.52F, 1.0F);
    c[ImGuiCol_WindowBg] = ImVec4(0.955F, 0.965F, 0.975F, 1.0F);
    c[ImGuiCol_ChildBg] = ImVec4(1.0F, 1.0F, 1.0F, 1.0F);
    c[ImGuiCol_PopupBg] = ImVec4(1.0F, 1.0F, 1.0F, 1.0F);
    c[ImGuiCol_Border] = ImVec4(0.68F, 0.73F, 0.78F, 1.0F);
    c[ImGuiCol_FrameBg] = ImVec4(1.0F, 1.0F, 1.0F, 1.0F);
    c[ImGuiCol_FrameBgHovered] = ImVec4(0.93F, 0.97F, 0.98F, 1.0F);
    c[ImGuiCol_FrameBgActive] = ImVec4(0.88F, 0.95F, 0.97F, 1.0F);
    c[ImGuiCol_Button] = ImVec4(0.90F, 0.92F, 0.94F, 1.0F);
    c[ImGuiCol_ButtonHovered] = ImVec4(0.83F, 0.88F, 0.91F, 1.0F);
    c[ImGuiCol_ButtonActive] = ImVec4(0.76F, 0.82F, 0.87F, 1.0F);
    c[ImGuiCol_Header] = ImVec4(0.05F, 0.50F, 0.58F, 0.16F);
    c[ImGuiCol_HeaderHovered] = ImVec4(0.05F, 0.50F, 0.58F, 0.26F);
    c[ImGuiCol_HeaderActive] = ImVec4(0.05F, 0.50F, 0.58F, 0.36F);
    c[ImGuiCol_TableHeaderBg] = ImVec4(0.92F, 0.94F, 0.96F, 1.0F);
    c[ImGuiCol_TableRowBgAlt] = ImVec4(0.97F, 0.98F, 0.99F, 1.0F);
    c[ImGuiCol_TableBorderStrong] = ImVec4(0.80F, 0.83F, 0.86F, 1.0F);
    c[ImGuiCol_TableBorderLight] = ImVec4(0.88F, 0.90F, 0.92F, 1.0F);
    c[ImGuiCol_CheckMark] = kAccent;
    c[ImGuiCol_SliderGrab] = kAccent;
    c[ImGuiCol_TextSelectedBg] = ImVec4(0.05F, 0.50F, 0.58F, 0.28F);
    c[ImGuiCol_Separator] = ImVec4(0.84F, 0.86F, 0.89F, 1.0F);
}

// ---- Layout -----------------------------------------------------------------------------

void heading(const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, kAccent);
    ImGui::SetWindowFontScale(1.35F);
    ImGui::TextUnformatted(text);
    ImGui::SetWindowFontScale(1.0F);
    ImGui::PopStyleColor();
    ImGui::Spacing();
}

void sectionTitle(const char* text) {
    ImGui::Spacing();
    ImGui::TextUnformatted(text);
    ImGui::Separator();
    ImGui::Spacing();
}

void muted(const char* text) { ImGui::TextDisabled("%s", text); }

void mutedWrapped(const std::string& text) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", text.c_str());
    ImGui::PopStyleColor();
}

void vspace(float units) { ImGui::Dummy(ImVec2(0.0F, ImGui::GetTextLineHeight() * 0.5F * units)); }

bool textField(const char* label, std::string& value, const char* hint, ImGuiInputTextFlags flags) {
    ImGui::PushID(label);
    ImGui::TextDisabled("%s", label);
    ImGui::SetNextItemWidth(-FLT_MIN);
    const bool changed = ImGui::InputTextWithHint("##value", hint != nullptr ? hint : "", &value, flags);
    ImGui::PopID();
    return changed;
}

bool passwordField(const char* label, std::string& value, ImGuiInputTextFlags flags) {
    return textField(label, value, nullptr, flags | ImGuiInputTextFlags_Password);
}

bool multilineField(const char* label, std::string& value, float lines) {
    ImGui::PushID(label);
    ImGui::TextDisabled("%s", label);
    const float height = ImGui::GetTextLineHeight() * lines + ImGui::GetStyle().FramePadding.y * 2.0F;
    const bool changed = ImGui::InputTextMultiline("##value", &value, ImVec2(-FLT_MIN, height));
    ImGui::PopID();
    return changed;
}

bool dateField(const char* label, std::string& value, const Date& today) {
    ImGui::PushID(label);
    ImGui::TextDisabled("%s", label);
    const float buttonWidth = ImGui::CalcTextSize("Today").x + ImGui::GetStyle().FramePadding.x * 2.0F;
    ImGui::SetNextItemWidth(ImGui::GetContentRegionAvail().x - buttonWidth - ImGui::GetStyle().ItemSpacing.x);
    bool changed = ImGui::InputTextWithHint("##value", "DD-MM-YYYY", &value);
    ImGui::SameLine();
    if (ImGui::Button("Today")) {
        value = today.toDisplay();
        changed = true;
    }
    ImGui::PopID();
    return changed;
}

bool primaryButton(const char* label, float width) {
    return coloredButton(label, width, kAccent, kAccentHover, kAccentDown);
}

bool dangerButton(const char* label, float width) {
    return coloredButton(label, width, kDanger, kDangerHover, kDangerDown);
}

bool plainButton(const char* label, float width) { return ImGui::Button(label, ImVec2(width, 0.0F)); }

bool beginTable(const char* id, std::initializer_list<const char*> columns, float height) {
    const ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable |
                                  ImGuiTableFlags_SizingStretchProp | ImGuiTableFlags_ScrollY;
    if (!ImGui::BeginTable(id, static_cast<int>(columns.size()), flags, ImVec2(0.0F, height))) return false;
    for (const char* name : columns) ImGui::TableSetupColumn(name);
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableHeadersRow();
    return true;
}

float rowsHeight(std::size_t rows, std::size_t most) {
    const float rowHeight = ImGui::GetTextLineHeight() + ImGui::GetStyle().CellPadding.y * 2.0F + 1.0F;
    return rowHeight * (static_cast<float>(std::min(rows, most)) + 1.0F) + 2.0F;
}

void keyValue(const char* key, const std::string& value) {
    ImGui::TextDisabled("%s", key);
    ImGui::SameLine(ImGui::GetFontSize() * 9.0F);
    ImGui::TextWrapped("%s", value.c_str());
}

// ---- Text -------------------------------------------------------------------------------

std::string phoneText(const PatientRecord& p) {
    if (p.phone) return phone::display(*p.phone);
    if (p.noPhoneConfirmed) return "No phone (confirmed at registration)";
    return "Not recorded (older record)";
}

std::string phoneCell(const PatientRecord& p) {
    if (p.phone) return phone::display(*p.phone);
    return p.noPhoneConfirmed ? "No phone" : "Not recorded";
}

std::string dobText(const PatientRecord& p, const Date& today) {
    const int age = ageInYears(p.dateOfBirth, today);
    if (p.dobEstimated) {
        return p.dateOfBirth.toDisplay() + " (estimated from age; about " + std::to_string(age) + " years)";
    }
    return p.dateOfBirth.toDisplay() + " (age " + std::to_string(age) + ")";
}

std::string dayText(const Date& d) {
    static const char* const kShort[7] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
    return std::string(kShort[isoWeekday(d) - 1]) + " " + d.toDisplay();
}

std::string rupees(std::int64_t paise) { return formatRupees(paise); }

Result<std::optional<WeeklyHours>> parseHoursText(const std::string& raw, int weekday) {
    const std::string t = text::asciiLower(text::trimSpaces(raw));
    if (t.empty() || t == "off") return std::optional<WeeklyHours>{};
    const auto dash = t.find('-');
    if (dash == std::string::npos) return Error::validation(weekdayName(weekday), "type hours as 09:00-13:00, or off");
    auto from = input::parseTimeOfDay(text::trimSpaces(t.substr(0, dash)), weekdayName(weekday));
    if (!from) return from.error();
    auto to = input::parseTimeOfDay(text::trimSpaces(t.substr(dash + 1)), weekdayName(weekday));
    if (!to) return to.error();
    return std::optional<WeeklyHours>{WeeklyHours{weekday, from.value(), to.value()}};
}

std::string hoursText(const DoctorSummary& d, int weekday) {
    const auto h = d.hoursOn(weekday);
    return h ? minuteText(h->startMinute) + "-" + minuteText(h->endMinute) : "off";
}

void patientCard(const PatientRecord& p, const Date& today, bool showUhid) {
    ImGui::PushID(static_cast<int>(p.uhid));
    if (ImGui::BeginChild("patientcard", ImVec2(0.0F, 0.0F), ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY)) {
        if (showUhid) keyValue("UHID", formatUhid(p.uhid));
        keyValue("Name", p.fullName);
        keyValue("Date of birth", dobText(p, today));
        keyValue("Gender", to_string(p.gender));
        keyValue("Address", p.address);
        keyValue("Mobile", phoneText(p));
    }
    ImGui::EndChild();
    ImGui::PopID();
}

}  // namespace hms::gui
