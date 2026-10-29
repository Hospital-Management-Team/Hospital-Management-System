#include "ui/console_app.hpp"

#include <ostream>

#include "consultation/consultation_service.hpp"
#include "patient/uhid.hpp"
#include "core/text.hpp"
#include "patient/patient_validation.hpp"
#include "patient/phone.hpp"
#include "validation/input.hpp"

namespace hms {

namespace {
const char* kRule = "------------------------------------------------------------------------------";

// Full text for record screens.
std::string phoneText(const PatientRecord& p) {
    if (p.phone) return phone::display(*p.phone);
    if (p.noPhoneConfirmed) return "No phone (confirmed at registration)";
    return "Not recorded (older record)";
}

// Short text for table columns.
std::string phoneCell(const PatientRecord& p) {
    if (p.phone) return phone::display(*p.phone);
    return p.noPhoneConfirmed ? "No phone" : "Not recorded";
}

// Typed at the mobile prompt to choose "no phone available".
bool meansNoPhone(const std::string& s) {
    const std::string v = text::asciiLower(text::trimSpaces(s));
    return v == "n" || v == "none" || v == "no phone";
}
}  // namespace

ConsoleApp::ConsoleApp(ConsoleIO& io, PatientService& patients, AppointmentService& appointments,
                       Session session)
    : io_(io), patients_(patients), appointments_(appointments), session_(std::move(session)) {}

std::string ConsoleApp::dobText(const PatientRecord& p) const {
    const int age = ageInYears(p.dateOfBirth, patients_.today());
    if (p.dobEstimated) {
        return p.dateOfBirth.toDisplay() + " (estimated from age; about " + std::to_string(age) + " years)";
    }
    return p.dateOfBirth.toDisplay() + " (age " + std::to_string(age) + ")";
}

int ConsoleApp::run() {
    auto& out = io_.out();
    const bool reception = session_.role == Role::Receptionist;
    while (!io_.exhausted()) {
        out << "\n" << kRule << "\n";
        out << (reception ? "RECEPTION" : "DOCTOR") << " MENU  -  " << session_.displayName;
        if (session_.simulated) out << "  [simulated session: login arrives with issue #3]";
        out << "\n" << kRule << "\n";
        if (reception) {
            out << "  1. Register a new patient\n"
                   "  2. Search patients (UHID, name, mobile number, or name and date of birth)\n"
                   "  3. Open a patient record by UHID\n"
                   "  4. Find a returning patient (mobile number first, then name and date of birth)\n"
                   "  5. Book an appointment\n"
                   "  6. Reschedule or cancel an appointment\n"
                   "  7. Appointment reminders (next "
                << appointments_.policy().appointment.reminderLeadHours << " hours)\n";
        } else {
            out << "  1. Search patients (UHID, name, mobile number, or name and date of birth)\n"
                   "  2. Open a patient record by UHID\n"
                   "  3. My appointments (OPD queue)\n";
            if (consultations_) {
                out << "  4. My appointments for the week\n"
                       "  5. Record a consultation\n"
                       "  6. Past consultations of a patient\n"
                       "  7. My weekly working hours\n";
            }
        }
        out << "  0. Exit\n";
        auto choice = io_.readLine("Choose an option: ");
        if (!choice) break;
        const std::string c = text::trimSpaces(*choice);
        if (c == "0") break;
        if (reception) {
            if (c == "1") registerPatientScreen();
            else if (c == "2") searchPatientsScreen();
            else if (c == "3") viewPatientByUhidScreen();
            else if (c == "4") findReturningPatientScreen();
            else if (c == "5") bookAppointmentScreen();
            else if (c == "6") changeAppointmentScreen();
            else if (c == "7") remindersScreen();
            else out << "  Please choose a number from 0 to 7.\n";
        } else {
            if (c == "1") searchPatientsScreen();
            else if (c == "2") viewPatientByUhidScreen();
            else if (c == "3") myQueueScreen();
            else if (consultations_ && c == "4") myWeekScreen();
            else if (consultations_ && c == "5") recordConsultationScreen();
            else if (consultations_ && c == "6") patientHistoryScreen();
            else if (consultations_ && c == "7") ownHoursScreen();
            else out << (consultations_ ? "  Please choose a number from 0 to 7.\n" : "  Please choose 0, 1, 2 or 3.\n");
        }
    }
    out << "Goodbye.\n";
    return 0;
}

void ConsoleApp::handleFailure(const Error& e) {
    io_.showError(e);
    if (e.kind == ErrorKind::OutcomeUnknown) {
        changesBlocked_ = true;
        io_.out() << "  ! No further changes can be made in this session. Please restart the application.\n";
    }
}

// ------------------------------------------------------------ registration

void ConsoleApp::registerPatientScreen() {
    auto& out = io_.out();
    if (changesBlocked_) {
        out << "  ! Changes are blocked until the application is restarted (an earlier save could not be "
               "confirmed).\n";
        return;
    }
    const PatientPolicy& policy = patients_.policy().patient;
    const Date today = patients_.today();

    out << "\nREGISTER A NEW PATIENT\n"
        << "Fields marked * are required. To cancel, press Ctrl+D (Linux) or Ctrl+Z then Enter (Windows).\n\n";

    PatientDraft draft;

    // * Full name
    auto name = io_.ask<std::string>("* Full name: ", [&](const std::string& s) {
        return patient_rules::validateFullName(s, policy);
    });
    if (!name) { out << "Registration cancelled. Nothing was saved.\n"; return; }
    draft.fullName = *name;

    // * Date of birth, or age if the date is not known
    while (true) {
        auto dob = io_.readLine("* Date of birth (DD-MM-YYYY), or press Enter if not known: ");
        if (!dob) { out << "Registration cancelled. Nothing was saved.\n"; return; }
        auto cleaned = input::cleanLine(*dob, patient_rules::kDateOfBirth);
        if (!cleaned) { io_.showError(cleaned.error()); continue; }
        if (cleaned.value().empty()) {
            auto age = io_.ask<std::string>("* Age in years: ", [&](const std::string& s) -> Result<std::string> {
                auto r = patient_rules::validateAge(s, today, policy);
                if (!r) return r.error();
                return text::trimSpaces(s);
            });
            if (!age) { out << "Registration cancelled. Nothing was saved.\n"; return; }
            draft.ageYears = *age;
            break;
        }
        auto checked = patient_rules::validateDateOfBirth(cleaned.value(), today, policy);
        if (!checked) { io_.showError(checked.error()); continue; }
        draft.dateOfBirth = cleaned.value();
        break;
    }

    // * Gender
    auto gender = io_.ask<Gender>("* Gender (M = Male, F = Female, O = Other): ",
                                  [](const std::string& s) { return patient_rules::validateGender(s); });
    if (!gender) { out << "Registration cancelled. Nothing was saved.\n"; return; }
    draft.gender = to_string(*gender);

    // * Address
    auto address = io_.ask<std::string>("* Address: ", [&](const std::string& s) {
        return patient_rules::validateAddress(s, policy);
    });
    if (!address) { out << "Registration cancelled. Nothing was saved.\n"; return; }
    draft.address = *address;

    // * Mobile number (deviation D-04): the patient's, or a family member's or
    //   carer's. Only when nobody has one, staff confirm "no phone available".
    //   A made-up number is never entered: it would match other patients.
    out << "  For a child or an elderly patient, a family member's or carer's number is fine.\n"
           "  If neither the patient nor anyone with them has a phone, type N.\n";
    while (true) {
        auto mobile = io_.ask<std::optional<std::string>>(
            "* Mobile number (or N if no phone is available): ",
            [](const std::string& s) -> Result<std::optional<std::string>> {
                if (meansNoPhone(s)) return std::optional<std::string>{};
                auto r = patient_rules::validatePhone(s);
                if (!r) return r.error();
                return std::optional<std::string>{r.value()};
            });
        if (!mobile) { out << "Registration cancelled. Nothing was saved.\n"; return; }
        if (*mobile) {
            draft.phone = **mobile;
            draft.noPhone = false;
            break;
        }
        auto sure = io_.askYesNo("Confirm: the patient AND everyone with them have no mobile number? (Y/N): ");
        if (!sure) { out << "Registration cancelled. Nothing was saved.\n"; return; }
        if (*sure) {
            draft.phone.clear();
            draft.noPhone = true;
            break;
        }
    }

    // Confirmation before any data-changing action (SRS 3.1).
    auto preview = patient_rules::validatePatient(draft, today, policy);
    if (!preview) { handleFailure(preview.error()); return; }
    PatientRecord shown;
    shown.fullName = preview.value().fullName;
    shown.dateOfBirth = preview.value().dateOfBirth;
    shown.dobEstimated = preview.value().dobEstimated;
    shown.gender = preview.value().gender;
    shown.address = preview.value().address;
    shown.phone = preview.value().phone;
    shown.noPhoneConfirmed = preview.value().noPhoneConfirmed;

    out << "\nPlease read these details back to the patient:\n";
    out << "  Name          : " << shown.fullName << "\n"
        << "  Date of birth : " << dobText(shown) << "\n"
        << "  Gender        : " << to_string(shown.gender) << "\n"
        << "  Address       : " << shown.address << "\n"
        << "  Mobile        : " << phoneText(shown) << "\n";
    auto confirm = io_.askYesNo("Save this patient? (Y/N): ");
    if (!confirm || !*confirm) { out << "Registration cancelled. Nothing was saved.\n"; return; }

    auto result = patients_.registerPatient(session_, draft, DuplicateDecision::none());
    if (!result) { handleFailure(result.error()); return; }
    if (result.value().status == RegistrationOutcome::Status::Registered) {
        out << "\n  Patient registered. UHID: " << formatUhid(result.value().uhid)
            << "\n  Write this UHID on the patient's card.\n";
        return;
    }
    reviewDuplicates(draft, std::move(result).value());
}

void ConsoleApp::showCandidates(const std::vector<Candidate>& candidates) {
    auto& out = io_.out();
    const Date today = patients_.today();
    out << "  #  Match       UHID        " << text::fitWidth("Name", 22) << " DOB         Age  "
        << text::fitWidth("Gender", 7) << text::fitWidth("Mobile", 16) << "Address\n";
    int i = 1;
    for (const auto& c : candidates) {
        const auto& p = c.patient;
        std::string match = c.kind == CandidateKind::Strong ? "STRONG" : "SAME PHONE";
        out << "  " << text::fitWidth(std::to_string(i++), 3) << text::fitWidth(match, 12)
            << text::fitWidth(formatUhid(p.uhid), 12) << text::fitWidth(p.fullName, 22) << " "
            << text::fitWidth(p.dateOfBirth.toDisplay() + (p.dobEstimated ? "*" : ""), 12)
            << text::fitWidth(std::to_string(ageInYears(p.dateOfBirth, today)), 5)
            << text::fitWidth(to_string(p.gender), 7)
            << text::fitWidth(phoneCell(p), 16) << text::fitWidth(p.address, 24)
            << (c.newSinceReview ? "  << NEW since last review" : "") << "\n";
    }
    out << "  STRONG = same name, date of birth and gender.  SAME PHONE = shares this mobile number.\n"
           "  * = date of birth estimated from age.\n";
}

void ConsoleApp::reviewDuplicates(const PatientDraft& draft, RegistrationOutcome outcome) {
    auto& out = io_.out();
    while (true) {
        const auto& cands = outcome.candidates;
        out << "\nPOSSIBLE EXISTING PATIENT" << (cands.size() > 1 ? "S" : "")
            << " FOUND - check with the patient before continuing.\n";
        showCandidates(cands);
        out << "\nChoose:\n";
        if (cands.size() == 1) {
            out << "  1     Same patient - use this existing UHID (no new record)\n";
        } else {
            out << "  1-" << cands.size() << "   Same patient - use that existing UHID (no new record)\n";
        }
        if (outcome.differentPersonAllowed) {
            out << "  D     Different person - create a new UHID\n";
        } else {
            out << "  (Hospital policy: a patient sharing this mobile number must use the existing UHID.)\n";
        }
        out << "  C     Cancel (nothing is saved)\n";

        auto line = io_.readLine("Your choice: ");
        if (!line) { out << "Registration cancelled. Nothing was saved.\n"; return; }
        const std::string choice = text::asciiLower(text::trimSpaces(*line));

        std::vector<std::int64_t> reviewed;
        for (const auto& c : cands) reviewed.push_back(c.patient.uhid);

        if (choice == "c") { out << "Registration cancelled. Nothing was saved.\n"; return; }

        if (choice == "d" && outcome.differentPersonAllowed) {
            DuplicateDecision decision;
            decision.kind = DuplicateDecision::Kind::DifferentPerson;
            decision.reviewedUhids = reviewed;
            if (outcome.reasonRequired) {
                out << "Why is this a different person?\n"
                    << "  1  " << reasonLabel(DuplicateReason::TwinsOrMultipleBirth) << "\n"
                    << "  2  " << reasonLabel(DuplicateReason::DifferentPersonSameNameAndDob) << "\n"
                    << "  3  " << reasonLabel(DuplicateReason::Other) << "\n";
                auto reason = io_.ask<DuplicateReason>("Reason (1-3): ", [](const std::string& s) -> Result<DuplicateReason> {
                    const std::string v = text::trimSpaces(s);
                    if (v == "1") return DuplicateReason::TwinsOrMultipleBirth;
                    if (v == "2") return DuplicateReason::DifferentPersonSameNameAndDob;
                    if (v == "3") return DuplicateReason::Other;
                    return Error::validation("Reason", "must be 1, 2 or 3");
                });
                if (!reason) { out << "Registration cancelled. Nothing was saved.\n"; return; }
                decision.reason = *reason;
            }
            auto result = patients_.registerPatient(session_, draft, decision);
            if (!result) { handleFailure(result.error()); return; }
            if (result.value().status == RegistrationOutcome::Status::Registered) {
                out << "\n  Patient registered as a different person. UHID: " << formatUhid(result.value().uhid)
                    << "\n  Write this UHID on the patient's card.\n";
                return;
            }
            out << "\n  Another possible match was registered while you were deciding. Please review again.\n";
            outcome = std::move(result).value();
            continue;
        }

        auto index = input::parseWholeNumber(choice, "Choice", 1, static_cast<std::int64_t>(cands.size()));
        if (index) {
            const std::int64_t uhid = cands[static_cast<std::size_t>(index.value() - 1)].patient.uhid;
            auto result = patients_.useExistingPatient(session_, draft, uhid, reviewed);
            if (!result) { handleFailure(result.error()); return; }
            out << "\n  No new record was created. Use the existing UHID " << formatUhid(uhid) << ":\n";
            showPatient(result.value());
            return;
        }
        out << "  Please choose a number from the list, " << (outcome.differentPersonAllowed ? "D or " : "")
            << "C.\n";
    }
}

// ---------------------------------------------------------------- search

void ConsoleApp::showPatientTable(const std::vector<PatientRecord>& rows) {
    auto& out = io_.out();
    const Date today = patients_.today();
    out << "  #   UHID        " << text::fitWidth("Name", 26) << " DOB          Age  " << text::fitWidth("Gender", 7)
        << "Mobile\n";
    int i = 1;
    for (const auto& p : rows) {
        out << "  " << text::fitWidth(std::to_string(i++), 4) << text::fitWidth(formatUhid(p.uhid), 12)
            << text::fitWidth(p.fullName, 26) << " "
            << text::fitWidth(p.dateOfBirth.toDisplay() + (p.dobEstimated ? "*" : ""), 13)
            << text::fitWidth(std::to_string(ageInYears(p.dateOfBirth, today)), 5)
            << text::fitWidth(to_string(p.gender), 7) << phoneCell(p) << "\n";
    }
}

void ConsoleApp::searchPatientsScreen() {
    auto& out = io_.out();
    out << "\nSEARCH PATIENTS\n  1. By UHID\n  2. By name (any part of the name)\n  3. By mobile number\n"
           "  4. By name and date of birth\n  0. Back\n";
    auto mode = io_.readLine("Search by: ");
    if (!mode) return;
    const std::string m = text::trimSpaces(*mode);
    if (m == "4") { nameAndDobSearch(); return; }
    SearchMode searchMode;
    std::string prompt;
    if (m == "1") { searchMode = SearchMode::Uhid; prompt = "UHID (e.g. HMS-000123): "; }
    else if (m == "2") { searchMode = SearchMode::Name; prompt = "Name or part of the name: "; }
    else if (m == "3") { searchMode = SearchMode::Phone; prompt = "Mobile number: "; }
    else return;

    while (true) {
        auto query = io_.readLine(prompt);
        if (!query) return;
        auto result = patients_.searchPatients(session_, searchMode, *query);
        if (!result) {
            handleFailure(result.error());
            if (result.error().kind == ErrorKind::Validation) continue;
            return;
        }
        if (result.value().rows.empty()) { out << "  No patient found.\n"; return; }
        pickFromResults(result.value(), "Enter a row number to open the record, or press Enter to go back: ");
        return;
    }
}

// Shows the rows and lets staff open one. Returns true if a record was opened.
bool ConsoleApp::pickFromResults(const SearchResult& r, const std::string& prompt) {
    auto& out = io_.out();
    out << "\n";
    showPatientTable(r.rows);
    if (r.truncated()) {
        out << "  Showing the first " << r.rows.size() << " of " << r.totalMatches
            << " matches. Type more of the name to narrow the search.\n";
    } else {
        out << "  " << r.totalMatches << (r.totalMatches == 1 ? " patient" : " patients") << " found.\n";
    }
    while (true) {
        auto pick = io_.readLine(prompt);
        if (!pick || text::trimSpaces(*pick).empty()) return false;
        auto idx = input::parseWholeNumber(text::trimSpaces(*pick), "Row", 1,
                                           static_cast<std::int64_t>(r.rows.size()));
        if (!idx) { io_.showError(idx.error()); continue; }
        showPatient(r.rows[static_cast<std::size_t>(idx.value() - 1)]);
        return true;
    }
}

// Name + date of birth. Returns true if a record was opened.
bool ConsoleApp::nameAndDobSearch() {
    auto& out = io_.out();
    while (true) {
        NameDobQuery q;
        auto name = io_.readLine("Name or part of the name: ");
        if (!name) return false;
        q.name = *name;
        auto dob = io_.readLine("Date of birth (DD-MM-YYYY): ");
        if (!dob) return false;
        q.dateOfBirth = *dob;
        auto result = patients_.searchByNameAndDob(session_, q);
        if (!result) {
            handleFailure(result.error());
            if (result.error().kind == ErrorKind::Validation) continue;
            return false;
        }
        if (result.value().rows.empty()) {
            out << "  No patient found with that name and date of birth.\n";
            return false;
        }
        out << "  (* = date of birth estimated from age; it matches within one year.)\n";
        return pickFromResults(result.value(),
                               "Confirm the name and date of birth with the patient, then enter the row number "
                               "(or press Enter if none of these is the patient): ");
    }
}

// Returning patient at the desk (HMS-F-004): ask for the mobile number first;
// one number can belong to a whole family, so staff pick the person from the
// list by name and date of birth. No number, or not in the list: search by
// name and date of birth.
void ConsoleApp::findReturningPatientScreen() {
    auto& out = io_.out();
    out << "\nFIND A RETURNING PATIENT\n"
           "  Ask for the mobile number registered with the hospital. If the patient has a UHID card,\n"
           "  use option 3 on the menu instead.\n";
    while (true) {
        auto line = io_.readLine("Mobile number (or press Enter if the patient has no phone): ");
        if (!line) return;
        if (text::trimSpaces(*line).empty()) break;
        auto result = patients_.searchPatients(session_, SearchMode::Phone, *line);
        if (!result) {
            handleFailure(result.error());
            if (result.error().kind == ErrorKind::Validation) continue;
            return;
        }
        if (result.value().rows.empty()) {
            out << "  No patient is registered with this number.\n";
            break;
        }
        if (result.value().rows.size() > 1) {
            out << "  This number is shared by " << result.value().rows.size()
                << " patients. Pick the person by name and date of birth.\n";
        }
        if (pickFromResults(result.value(),
                            "Confirm the name and date of birth with the patient, then enter the row number "
                            "(or press Enter if the patient is not listed): ")) {
            return;
        }
        out << "  Not in this list.\n";
        break;
    }
    out << "\nSearch by name and date of birth instead:\n";
    if (!nameAndDobSearch()) {
        out << "  If the patient has never been registered, use option 1 to register them.\n";
    }
}

void ConsoleApp::viewPatientByUhidScreen() {
    auto& out = io_.out();
    auto line = io_.readLine("UHID (e.g. HMS-000123): ");
    if (!line) return;
    auto id = parseUhid(*line);
    if (!id) { out << "  ! UHID: must look like HMS-000123 or 123\n"; return; }
    auto r = patients_.getPatient(session_, *id);
    if (!r) { handleFailure(r.error()); return; }
    showPatient(r.value());
}

void ConsoleApp::showPatient(const PatientRecord& p) {
    auto& out = io_.out();
    out << "\n  PATIENT RECORD (demographic details)\n"
        << "  UHID          : " << formatUhid(p.uhid) << "\n"
        << "  Name          : " << p.fullName << "\n"
        << "  Date of birth : " << dobText(p) << "\n"
        << "  Gender        : " << to_string(p.gender) << "\n"
        << "  Address       : " << p.address << "\n"
        << "  Mobile        : " << phoneText(p) << "\n";
}

}  // namespace hms
