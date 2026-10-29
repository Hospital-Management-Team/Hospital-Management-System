// Doctor workspace screens of ConsoleApp (issue #5): the weekly queue, recording
// a consultation, a patient's earlier consultations, and the doctor's own weekly
// hours. Like the rest of ConsoleApp, these screens hold no business rule: they
// read input, call one service function and show the result.

#include <ostream>

#include "consultation/consultation_service.hpp"
#include "core/text.hpp"
#include "patient/uhid.hpp"
#include "ui/console_app.hpp"
#include "ui/doctor_schedule_screen.hpp"
#include "validation/input.hpp"

namespace hms {

namespace {
const char* kCancelled = "Cancelled. Nothing was saved.\n";

std::string dayText(const Date& d) {
    static const char* kShort[7] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
    return std::string(kShort[isoWeekday(d) - 1]) + " " + d.toDisplay();
}

void showConsultation(std::ostream& out, const ConsultationRecord& c) {
    out << "\n  " << formatConsultationId(c.consultationId) << "  visit " << formatAppointmentId(c.appointmentId) << " on "
        << c.slot.toDisplay() << "  Dr " << c.doctorUsername << "\n"
        << "    Patient   : " << c.patientName << " (" << formatUhid(c.uhid) << ")\n"
        << "    Diagnosis : " << c.diagnosis << "\n";
    for (const auto& p : c.prescriptions) {
        out << "    Medicine  : " << p.medicineName << " - " << p.dosage << " - quantity " << p.quantity << "\n";
    }
    for (const auto& l : c.labOrders) {
        out << "    Test      : " << l.testName << (l.instructions ? " (" + *l.instructions + ")" : "") << "\n";
    }
}
}  // namespace

// ------------------------------------------------------------ weekly queue (HMS-F-021)

void ConsoleApp::myWeekScreen() {
    auto& out = io_.out();
    auto from = askDate("First day (DD-MM-YYYY), or press Enter for today: ", appointments_.today());
    if (!from) return;
    auto r = appointments_.myWeekQueue(session_, *from);
    if (!r) { handleFailure(r.error()); return; }
    const Date last = civilFromDays(daysFromCivil(from->year, from->month, from->day) + 6);
    out << "\nMY APPOINTMENTS - " << session_.displayName << " - " << dayText(*from) << " to " << dayText(last) << "\n";
    if (r.value().empty()) {
        out << "  No appointments.\n";
        return;
    }
    out << "  " << text::fitWidth("Date", 15) << text::fitWidth("Time", 7) << text::fitWidth("Appointment", 13)
        << text::fitWidth("UHID", 12) << text::fitWidth("Patient", 24) << "Status\n";
    for (const auto& a : r.value()) {
        out << "  " << text::fitWidth(dayText(a.start.date), 15) << text::fitWidth(a.start.timeText(), 7)
            << text::fitWidth(formatAppointmentId(a.id), 13) << text::fitWidth(formatUhid(a.uhid), 12)
            << text::fitWidth(a.patientName, 24) << to_string(a.status) << "\n";
    }
    out << "  " << r.value().size() << " appointment" << (r.value().size() == 1 ? "" : "s") << ".\n";
}

// ------------------------------------------------------------ consultation (HMS-F-022)

void ConsoleApp::recordConsultationScreen() {
    auto& out = io_.out();
    if (blockedForChanges()) return;
    const ConsultationPolicy& cp = consultations_->policy().consultation;
    out << "\nRECORD A CONSULTATION\n"
        << "Fields marked * are required. To cancel, press Enter at the first prompt, or Ctrl+D (Linux) / Ctrl+Z then "
           "Enter (Windows).\n";

    std::int64_t appointmentId = 0;
    {
        auto line = io_.readLine("* Appointment number, e.g. APT-000012 (see option 3 or 4): ");
        if (!line || text::trimSpaces(*line).empty()) return;
        auto id = parseAppointmentId(text::trimSpaces(*line));
        while (!id) {
            out << "  ! Appointment: must look like APT-000012 or 12\n";
            line = io_.readLine("* Appointment number: ");
            if (!line || text::trimSpaces(*line).empty()) return;
            id = parseAppointmentId(text::trimSpaces(*line));
        }
        appointmentId = *id;
    }

    ConsultationDraft draft;
    draft.appointmentId = appointmentId;
    auto diagnosis = io_.ask<std::string>("* Diagnosis: ", [&](const std::string& s) -> Result<std::string> {
        auto v = input::requireLine(s, "Diagnosis");
        if (!v) return v.error();
        if (text::utf8Length(v.value()) > cp.maxDiagnosisChars) {
            return Error::validation("Diagnosis", "must be at most " + std::to_string(cp.maxDiagnosisChars) + " characters");
        }
        return v.value();
    });
    if (!diagnosis) { out << kCancelled; return; }
    draft.diagnosis = *diagnosis;

    while (draft.prescriptions.size() < cp.maxItemsPerConsultation) {
        auto more = io_.askYesNo(draft.prescriptions.empty() ? "Prescribe a medicine? (Y/N): "
                                                             : "Prescribe another medicine? (Y/N): ");
        if (!more) { out << kCancelled; return; }
        if (!*more) break;
        PrescriptionDraft p;
        auto name = io_.ask<std::string>("  * Medicine: ", [&](const std::string& s) -> Result<std::string> {
            auto v = input::requireLine(s, "Medicine");
            if (!v) return v.error();
            if (text::utf8Length(v.value()) > cp.maxNameChars) {
                return Error::validation("Medicine", "must be at most " + std::to_string(cp.maxNameChars) + " characters");
            }
            return v.value();
        });
        if (!name) { out << kCancelled; return; }
        auto dosage = io_.ask<std::string>("  * Dosage (for example 1 tablet twice a day for 5 days): ",
                                           [&](const std::string& s) -> Result<std::string> {
            auto v = input::requireLine(s, "Dosage");
            if (!v) return v.error();
            if (text::utf8Length(v.value()) > cp.maxDosageChars) {
                return Error::validation("Dosage", "must be at most " + std::to_string(cp.maxDosageChars) + " characters");
            }
            return v.value();
        });
        if (!dosage) { out << kCancelled; return; }
        auto qty = io_.ask<std::string>("  * Total quantity: ", [&](const std::string& s) -> Result<std::string> {
            auto n = input::parseWholeNumber(text::trimSpaces(s), "Quantity", 1, cp.maxQuantity);
            if (!n) return n.error();
            return text::trimSpaces(s);
        });
        if (!qty) { out << kCancelled; return; }
        p.medicineName = *name;
        p.dosage = *dosage;
        p.quantity = *qty;
        draft.prescriptions.push_back(p);
    }

    while (draft.labOrders.size() < cp.maxItemsPerConsultation) {
        auto more = io_.askYesNo(draft.labOrders.empty() ? "Recommend a laboratory test? (Y/N): "
                                                         : "Recommend another test? (Y/N): ");
        if (!more) { out << kCancelled; return; }
        if (!*more) break;
        LabOrderDraft l;
        auto name = io_.ask<std::string>("  * Test: ", [&](const std::string& s) -> Result<std::string> {
            auto v = input::requireLine(s, "Test");
            if (!v) return v.error();
            if (text::utf8Length(v.value()) > cp.maxNameChars) {
                return Error::validation("Test", "must be at most " + std::to_string(cp.maxNameChars) + " characters");
            }
            return v.value();
        });
        if (!name) { out << kCancelled; return; }
        auto note = io_.ask<std::string>("    Instructions (optional): ", [&](const std::string& s) -> Result<std::string> {
            auto v = input::cleanLine(s, "Instructions");
            if (!v) return v.error();
            if (text::utf8Length(v.value()) > cp.maxInstructionChars) {
                return Error::validation("Instructions", "must be at most " + std::to_string(cp.maxInstructionChars) + " characters");
            }
            return v.value();
        });
        if (!note) { out << kCancelled; return; }
        l.testName = *name;
        l.instructions = *note;
        draft.labOrders.push_back(l);
    }

    out << "\nPlease check before saving (" << formatAppointmentId(appointmentId) << "):\n  Diagnosis : " << draft.diagnosis << "\n  Medicines : "
        << draft.prescriptions.size() << "\n  Tests     : " << draft.labOrders.size() << "\n";
    auto confirm = io_.askYesNo("Save this consultation? (Y/N): ");
    if (!confirm || !*confirm) { out << kCancelled; return; }

    auto r = consultations_->recordConsultation(session_, draft);
    if (!r) { handleFailure(r.error()); return; }
    out << "  Saved as " << formatConsultationId(r.value().consultationId) << ". The appointment is now Completed.";
    showConsultation(out, r.value());
}

void ConsoleApp::patientHistoryScreen() {
    auto& out = io_.out();
    std::int64_t uhid = 0;
    while (true) {
        auto line = io_.readLine("Patient UHID (Enter to go back): ");
        if (!line || text::trimSpaces(*line).empty()) return;
        auto id = parseUhid(*line);
        if (id) { uhid = *id; break; }
        out << "  ! UHID: must look like HMS-000123 or 123\n";
    }
    auto r = consultations_->listPatientHistory(session_, uhid);
    if (!r) { handleFailure(r.error()); return; }
    out << "\nPAST CONSULTATIONS of " << formatUhid(uhid) << " (newest first)\n";
    if (r.value().empty()) {
        out << "  No consultations recorded.\n";
        return;
    }
    for (const auto& c : r.value()) showConsultation(out, c);
}

void ConsoleApp::ownHoursScreen() { hms::ownHoursScreen(io_, appointments_, session_); }

}  // namespace hms
