#pragma once
// Administration and reporting types (SRS HMS-F-023, HMS-F-051; issue #9).

#include <cstdint>
#include <string>
#include <vector>

namespace hms {

// One staff account as the Admin sees it. The password hash never leaves the data store.
struct StaffMember {
    std::int64_t staffId = 0;
    std::string username;
    std::string role;        // "Doctor", "Receptionist", ...
    bool active = true;
    bool locked = false;     // locked after repeated failed logins, now (cooldown not over)
};

struct Department {
    std::int64_t departmentId = 0;
    std::string name;
    bool active = true;
    std::int64_t doctors = 0;   // doctors whose profile is in this department
};

struct ConsultationRoom {
    std::int64_t roomId = 0;
    std::string name;
    std::string department;     // empty when the room is not tied to a department
    bool active = true;
};

struct DoctorMapping {
    std::int64_t staffId = 0;
    std::string username;
    std::string department;
};

struct DayActivity {
    std::string date;                // "YYYY-MM-DD", hospital-local
    std::int64_t patientsSeen = 0;   // different patients with a booked or completed appointment that day
    std::int64_t booked = 0;
    std::int64_t completed = 0;
    std::int64_t cancelled = 0;
    std::int64_t noShow = 0;
};

struct DoctorActivity {
    std::string doctor;              // username
    std::string department;
    std::int64_t booked = 0;
    std::int64_t completed = 0;
    std::int64_t cancelled = 0;
    std::int64_t noShow = 0;
    std::int64_t total() const { return booked + completed + cancelled + noShow; }
};

struct ModeTotal {
    std::string mode;                // CASH, CARD, INSURANCE
    std::int64_t paise = 0;
};

struct RoleCount {
    std::string role;
    std::int64_t active = 0;
    std::int64_t deactivated = 0;
};

// The summary for a date range (inclusive, hospital-local dates).
struct HospitalReport {
    std::string from;                // "YYYY-MM-DD"
    std::string to;
    // Patients
    std::int64_t registeredPatients = 0;      // all patients in the store (not limited to the range)
    std::int64_t patientsSeen = 0;            // different patients with a booked or completed appointment in the range
    // Appointments (by slot date)
    std::int64_t appointments = 0;
    std::int64_t booked = 0;
    std::int64_t completed = 0;
    std::int64_t cancelled = 0;
    std::int64_t noShow = 0;
    std::int64_t consultations = 0;           // recorded in the range
    // Billing (bills by creation date, payments by payment date)
    std::int64_t billsCount = 0;
    std::int64_t billedPaise = 0;
    std::int64_t paymentsCount = 0;
    std::int64_t collectedPaise = 0;          // revenue: payments received in the range
    std::int64_t outstandingPaise = 0;        // billed in the range and still unpaid today
    std::vector<ModeTotal> byMode;
    std::vector<DayActivity> days;            // days with at least one appointment
    std::vector<DoctorActivity> doctors;      // doctors with at least one appointment
    std::vector<RoleCount> staff;             // accounts per role
};

}  // namespace hms
