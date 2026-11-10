#pragma once
// Widgets and text helpers shared by the screens of the graphical front end.
// Like the console screens, nothing here holds a business rule: screens read input,
// call one service function and show the result (SAD 4.4, "Ownership").

#include <cstdint>
#include <initializer_list>
#include <string>

#include <optional>

#include "appointment/appointment.hpp"
#include "core/clock.hpp"
#include "core/result.hpp"
#include "imgui.h"
#include "patient/patient.hpp"

namespace hms::gui {

// A message shown at the top of a screen: green for a success, red for a refused action.
class Notice {
public:
    void info(std::string text);
    void error(const Error& e);
    void error(std::string text);
    void clear();
    bool empty() const { return kind_ == Kind::None; }
    void draw() const;

private:
    enum class Kind { None, Info, Error };
    Kind kind_ = Kind::None;
    std::string text_;
};

// ---- Look ---------------------------------------------------------------------------
void applyTheme(float scale);

// ---- Layout -------------------------------------------------------------------------
void heading(const char* text);
void sectionTitle(const char* text);
void muted(const char* text);
void mutedWrapped(const std::string& text);
void vspace(float units = 1.0F);  // in multiples of the line height / 2

// A caption above a full-width text box. Returns true when the text changed.
bool textField(const char* label, std::string& value, const char* hint = nullptr, ImGuiInputTextFlags flags = 0);
bool passwordField(const char* label, std::string& value, ImGuiInputTextFlags flags = 0);
bool multilineField(const char* label, std::string& value, float lines = 3.0F);
// A date typed as DD-MM-YYYY, with a button that fills in today's date.
bool dateField(const char* label, std::string& value, const Date& today);

bool primaryButton(const char* label, float width = 0.0F);
bool dangerButton(const char* label, float width = 0.0F);
bool plainButton(const char* label, float width = 0.0F);

// A table with the given column headings. Call ImGui::EndTable() when this returns true.
bool beginTable(const char* id, std::initializer_list<const char*> columns, float height = 0.0F);

// The height that fits a heading row and `rows` table rows (at most `most` rows before it scrolls).
float rowsHeight(std::size_t rows, std::size_t most = 10);

// Label and value on one line.
void keyValue(const char* key, const std::string& value);

// ---- Text ---------------------------------------------------------------------------
std::string phoneText(const PatientRecord& p);   // full text for record screens
std::string phoneCell(const PatientRecord& p);   // short text for table cells
std::string dobText(const PatientRecord& p, const Date& today);
std::string dayText(const Date& d);              // "Wed 11-11-2026"
std::string rupees(std::int64_t paise);          // "Rs 750.00"

// "09:00-13:00" or "off" typed for a weekday  ->  the hours (nothing for "off"), or the field error.
Result<std::optional<WeeklyHours>> parseHoursText(const std::string& raw, int weekday);
std::string hoursText(const DoctorSummary& d, int weekday);  // "09:00-13:00" or "off"

// Shows the demographic details of a patient as a small card.
void patientCard(const PatientRecord& p, const Date& today, bool showUhid = true);

}  // namespace hms::gui
