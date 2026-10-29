#pragma once
// Role menus and screens for the console UI. Holds no business rules: it
// reads and parses input, calls one service function and shows the result
// (SAD 4.4, "Ownership").

#include <optional>
#include <vector>

#include "appointment/appointment_service.hpp"
#include "auth/session.hpp"
#include "patient/patient.hpp"
#include "patient/patient_service.hpp"
#include "ui/console_io.hpp"

namespace hms {

class ConsultationService;

class ConsoleApp {
public:
    ConsoleApp(ConsoleIO& io, PatientService& patients, AppointmentService& appointments, Session session);

    // Runs the role menu until the user exits. Returns the process exit code.
    int run();

    // Issue #5: gives the Doctor menu its workspace entries (weekly queue, record a
    // consultation, past consultations, own weekly hours). Without it the Doctor
    // menu is the one from issue #4.
    void setConsultationService(ConsultationService& consultations) { consultations_ = &consultations; }

private:
    void registerPatientScreen();
    void searchPatientsScreen();
    void viewPatientByUhidScreen();
    void findReturningPatientScreen();
    bool nameAndDobSearch();
    bool pickFromResults(const SearchResult& r, const std::string& prompt);

    // Duplicate review screen (HMS-F-002(9)); loops until staff register,
    // use an existing record, or cancel.
    void reviewDuplicates(const PatientDraft& draft, RegistrationOutcome outcome);

    // Appointments (issue #4)
    void bookAppointmentScreen();
    void changeAppointmentScreen();
    void remindersScreen();
    void myQueueScreen();
    // Doctor workspace (issue #5)
    void myWeekScreen();
    void recordConsultationScreen();
    void patientHistoryScreen();
    void ownHoursScreen();
    std::optional<PatientRecord> askPatient();
    std::optional<Date> askDate(const std::string& prompt, std::optional<Date> enterMeans);
    std::optional<LocalSlot> pickSlot(const std::vector<LocalSlot>& slots);
    bool blockedForChanges();

    void showPatient(const PatientRecord& p);
    void showPatientTable(const std::vector<PatientRecord>& rows);
    void showCandidates(const std::vector<Candidate>& candidates);
    void handleFailure(const Error& e);
    std::string dobText(const PatientRecord& p) const;

    ConsoleIO& io_;
    PatientService& patients_;
    AppointmentService& appointments_;
    ConsultationService* consultations_ = nullptr;
    Session session_;
    bool changesBlocked_ = false;  // set after an "outcome uncertain" result (SRS 2.5)
};

}  // namespace hms
