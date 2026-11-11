#pragma once
// "Patient UHID" box with a Look up button, used by every screen that works on one patient.
// It follows Context::currentUhid, so a patient found on one screen is already selected on
// the next.

#include <cstdint>
#include <optional>
#include <string>

#include "gui/context.hpp"
#include "patient/patient.hpp"

namespace hms::gui {

class PatientPicker {
public:
    // fetchRecord = false is for roles that may not open patient records (Lab Technician, Pharmacist):
    // the box then only checks that the UHID is well formed, as the console menus do.
    explicit PatientPicker(bool fetchRecord = true) : fetchRecord_(fetchRecord) {}

    // Draws the box. Returns true in the frame in which the selected patient changed.
    bool draw(Context& ctx, const char* label = "Patient UHID (for example HMS-000123, or 123)");

    const std::optional<PatientRecord>& patient() const { return patient_; }
    bool hasUhid() const { return uhid_ != 0; }
    std::int64_t uhid() const { return uhid_; }
    void clear();

private:
    bool lookUp(Context& ctx, const std::string& text);

    bool fetchRecord_ = true;
    std::int64_t uhid_ = 0;
    std::string text_;
    std::optional<PatientRecord> patient_;
    std::int64_t followed_ = 0;  // the Context::currentUhid already adopted
    Notice notice_;
};

}  // namespace hms::gui
