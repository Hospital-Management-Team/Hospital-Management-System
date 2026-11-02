// Medical history screen of ConsoleApp (issue #6, HMS-F-003). Like the rest of
// ConsoleApp it holds no business rule: the care-relationship check, the
// version rules and the audit entries are all in MedicalRecordService.

#include <ostream>

#include "billing/receipt.hpp"
#include "core/text.hpp"
#include "patient/uhid.hpp"
#include "record/medical_record_service.hpp"
#include "ui/console_app.hpp"

namespace hms {

namespace {
const char* kCancelled = "Cancelled. Nothing was saved.\n";

std::string orNone(const std::string& s) { return s.empty() ? "(none recorded)" : s; }

void showVersion(std::ostream& out, const MedicalHistoryVersion& v, int utcOffsetMinutes) {
    out << "  " << formatHistoryVersionId(v.versionId) << "  recorded " << localDateTimeText(v.recordedAt, utcOffsetMinutes)
        << " by Dr " << v.authorUsername << "\n"
        << "    Medical history     : " << orNone(v.history) << "\n"
        << "    Allergies           : " << orNone(v.allergies) << "\n"
        << "    Current medications : " << orNone(v.currentMedications) << "  (maintained by the doctor)\n";
}
}  // namespace

void ConsoleApp::medicalHistoryScreen() {
    auto& out = io_.out();
    std::int64_t uhid = 0;
    while (true) {
        auto line = io_.readLine("Patient UHID (Enter to go back): ");
        if (!line || text::trimSpaces(*line).empty()) return;
        auto id = parseUhid(*line);
        if (id) { uhid = *id; break; }
        out << "  ! UHID: must look like HMS-000123 or 123\n";
    }
    const int offset = medicalRecords_->policy().utcOffsetMinutes;

    // Opened (and audited) once, and again only after a new version is saved.
    MedicalHistory h;
    bool reload = true;
    while (true) {
        if (reload) {
            auto r = medicalRecords_->openHistory(session_, uhid);
            if (!r) { handleFailure(r.error()); return; }
            h = r.value();
            reload = false;
        }
        out << "\nMEDICAL HISTORY of " << h.patientName << " (" << formatUhid(h.uhid) << ")\n";
        if (const auto* current = h.current()) {
            showVersion(out, *current, offset);
            out << "  " << h.versions.size() << " version" << (h.versions.size() == 1 ? "" : "s") << " in all.\n";
        } else {
            out << "  No medical history recorded.\n";
        }

        out << "  1. Update (saves a new version)\n";
        if (h.versions.size() > 1) out << "  2. Show earlier versions\n";
        auto choice = io_.readLine("Choose an option, or press Enter to go back: ");
        if (!choice) return;
        const std::string c = text::trimSpaces(*choice);
        if (c.empty()) return;
        if (c == "2" && h.versions.size() > 1) {
            out << "\nEARLIER VERSIONS (newest first)\n";
            for (std::size_t i = 1; i < h.versions.size(); ++i) showVersion(out, h.versions[i], offset);
            continue;
        }
        if (c != "1") {
            out << "  Please choose " << (h.versions.size() > 1 ? "1 or 2" : "1") << ", or press Enter.\n";
            continue;
        }

        if (blockedForChanges()) return;
        MedicalHistoryDraft draft;
        if (const auto* current = h.current()) {
            draft = MedicalHistoryDraft{current->history, current->allergies, current->currentMedications};
        }
        out << "\nUPDATE MEDICAL HISTORY. For each field press Enter to keep it, or type - to clear it.\n";
        struct Field { const char* label; std::string* value; };
        const Field fields[] = {{"Medical history", &draft.history},
                                {"Allergies", &draft.allergies},
                                {"Current medications (maintained by the doctor)", &draft.currentMedications}};
        bool cancelled = false;
        for (const auto& f : fields) {
            out << "  " << f.label << " now: " << orNone(*f.value) << "\n";
            auto line = io_.readLine(std::string("  ") + f.label + ": ");
            if (!line) { cancelled = true; break; }
            const std::string typed = text::trimSpaces(*line);
            if (typed == "-") f.value->clear();
            else if (!typed.empty()) *f.value = typed;
        }
        if (cancelled) { out << kCancelled; return; }

        auto confirm = io_.askYesNo("Save this as a new version? (Y/N): ");
        if (!confirm || !*confirm) { out << kCancelled; continue; }
        auto saved = medicalRecords_->updateHistory(session_, uhid, draft);
        if (!saved) { handleFailure(saved.error()); continue; }
        out << "  Saved as " << formatHistoryVersionId(saved.value().versionId)
            << ". The earlier versions are kept unchanged.\n";
        reload = true;
    }
}

}  // namespace hms
