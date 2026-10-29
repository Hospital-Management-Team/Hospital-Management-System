#pragma once
// Appointment Module - storage (SAD 3.4, Data Access). Runs statements only;
// every business rule is in AppointmentService. Callers own the transaction.

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "appointment/appointment.hpp"
#include "data/database.hpp"

namespace hms {

class AppointmentRepository {
public:
    explicit AppointmentRepository(Database& db) : db_(db) {}

    // Doctor accounts (role 'doctor'), with their profile and hours if set.
    std::vector<DoctorSummary> listDoctors();
    std::optional<DoctorSummary> findDoctor(std::int64_t staffId);
    std::optional<std::int64_t> doctorIdForUsername(const std::string& username);

    // Replaces the profile and all weekly hours of one doctor.
    void saveSchedule(std::int64_t staffId, const DoctorScheduleDraft& draft, const std::string& updatedAtUtc);

    bool patientExists(std::int64_t uhid);

    std::optional<AppointmentRecord> findAppointment(std::int64_t id);

    // Booked appointments of one doctor, or one patient, on one date.
    std::vector<AppointmentRecord> bookedForDoctorOn(std::int64_t staffId, const Date& date);
    std::vector<AppointmentRecord> bookedForPatientOn(std::int64_t uhid, const Date& date);
    // Booked appointments of one doctor starting at or after `fromKey`.
    std::vector<AppointmentRecord> bookedForDoctorFrom(std::int64_t staffId, const std::string& fromKey);
    // Booked appointments of one patient starting at or after `fromKey`, in time order.
    std::vector<AppointmentRecord> bookedForPatientFrom(std::int64_t uhid, const std::string& fromKey);
    // A doctor's queue for one date: every appointment that is not cancelled, in time order.
    std::vector<AppointmentRecord> queueFor(std::int64_t staffId, const Date& date);
    // Booked appointments with fromKey <= start <= toKey, in time order, each
    // with whether a reminder entry already exists.
    std::vector<std::pair<AppointmentRecord, bool>> bookedBetween(const std::string& fromKey, const std::string& toKey);

    std::int64_t insertAppointment(std::int64_t uhid, std::int64_t staffId, const LocalSlot& start, int slotMinutes,
                                   const std::string& createdAtUtc, const std::string& createdBy);
    void markCancelled(std::int64_t id, const std::string& atUtc, const std::string& by);
    // Set by the consultation feature (#5) when the doctor records the visit.
    void markCompleted(std::int64_t id);
    void setReplacedBy(std::int64_t id, std::int64_t replacementId);
    void insertReminder(std::int64_t id, const std::string& atUtc, const std::string& by);

private:
    std::vector<WeeklyHours> hoursFor(std::int64_t staffId);

    Database& db_;
};

}  // namespace hms
