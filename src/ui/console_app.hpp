#pragma once
// Role menus and screens for the console UI. Holds no business rules: it
// reads and parses input, calls one service function and shows the result
// (SAD 4.4, "Ownership").

#include <optional>
#include <vector>

#include "auth/session.hpp"
#include "patient/patient.hpp"
#include "patient/patient_service.hpp"
#include "ui/console_io.hpp"

namespace hms {

class ConsoleApp {
public:
    ConsoleApp(ConsoleIO& io, PatientService& patients, Session session);

    // Runs the role menu until the user exits. Returns the process exit code.
    int run();

private:
    void registerPatientScreen();
    void searchPatientsScreen();
    void viewPatientByUhidScreen();

    // Duplicate review screen (HMS-F-002(9)); loops until staff register,
    // use an existing record, or cancel.
    void reviewDuplicates(const PatientDraft& draft, RegistrationOutcome outcome);

    void showPatient(const PatientRecord& p);
    void showPatientTable(const std::vector<PatientRecord>& rows);
    void showCandidates(const std::vector<Candidate>& candidates);
    void handleFailure(const Error& e);
    std::string dobText(const PatientRecord& p) const;

    ConsoleIO& io_;
    PatientService& patients_;
    Session session_;
    bool changesBlocked_ = false;  // set after an "outcome uncertain" result (SRS 2.5)
};

}  // namespace hms
