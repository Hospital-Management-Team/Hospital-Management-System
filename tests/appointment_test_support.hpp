#pragma once
// Fixtures for the appointment tests (issue #4). The fixed clock is
// Sunday 25-10-2026 11:30 IST, so Monday 26-10-2026 is "tomorrow".

#include <cstdint>
#include <string>

#include "appointment/appointment_service.hpp"
#include "patient_test_support.hpp"

namespace hms::test {

inline Session admin() { return Session{"admin1", "Test admin", Role::Admin, false}; }
inline Session doctorSession(const std::string& username) { return Session{username, username, Role::Doctor, false}; }

inline const Date kToday{2026, 10, 25};     // Sunday
inline const Date kMonday{2026, 10, 26};
inline const Date kTuesday{2026, 10, 27};

inline LocalSlot at(const Date& d, int hour, int minute) { return LocalSlot{d, hour * 60 + minute}; }

struct AppointmentFixture : StoreFixture {
    // A Doctor account written straight to the store (password hashing is
    // tested in test_authentication.cpp and is slow).
    std::int64_t addStaff(const std::string& username, const std::string& role = "doctor") {
        auto st = db().prepare("INSERT INTO staff_accounts (username, password_hash, role) VALUES (?, 'x', ?)");
        st.bind(1, username).bind(2, role);
        st.run();
        return db().lastInsertRowId();
    }

    // Department set and the same hours on every day of the week.
    void setHours(std::int64_t staffId, int startMinute, int endMinute, const std::string& dept = "General Medicine") {
        DoctorScheduleDraft d;
        d.department = dept;
        for (int day = 1; day <= 7; ++day) d.hours.push_back(WeeklyHours{day, startMinute, endMinute});
        auto r = appointments->setDoctorSchedule(admin(), staffId, d);
        REQUIRE(r.ok());
    }

    // A Doctor working 09:00-13:00 every day.
    std::int64_t addDoctor(const std::string& username, const std::string& dept = "General Medicine") {
        const std::int64_t id = addStaff(username);
        setHours(id, 9 * 60, 13 * 60, dept);
        return id;
    }

    std::int64_t addPatient(const std::string& name) {
        auto r = service->registerPatient(receptionist(), draft(name, "05-03-1990", "M"), DuplicateDecision::none());
        REQUIRE(r.ok());
        REQUIRE(r.value().status == RegistrationOutcome::Status::Registered);
        return r.value().uhid;
    }

    AppointmentRecord bookOk(std::int64_t uhid, std::int64_t doctorId, const LocalSlot& start) {
        auto r = appointments->book(receptionist(), uhid, doctorId, start);
        REQUIRE_MESSAGE(r.ok(), (r.ok() ? "" : r.error().describe()));
        return r.value();
    }

    // Moves the clock to a hospital-local time.
    void setLocalTime(const Date& d, int hour, int minute) {
        clock.set(makeUtc(d.year, d.month, d.day, hour, minute) - std::chrono::minutes(policy.utcOffsetMinutes));
    }
};

}  // namespace hms::test
