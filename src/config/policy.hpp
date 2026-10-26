#pragma once
// Build-time policy values that the SRS marks [TEAM DECISION] or that are
// likely to change when the SRS/SAD/STP are revised. Keeping them in one
// place means a document change becomes a one-line code change plus a test
// update. Each feature adds its own policy struct here.
//
// Values that Admin edits at run time (HMS-F-052: cutoff, reminder window,
// lockout cooldown, low-stock threshold) belong in a system_parameter table.

#include <cstddef>

#include "core/clock.hpp"

namespace hms {

// Patient registration and search (issue #2).
struct PatientPolicy {
    // Full name 1-100 characters, address 1-200 (SRS v11 draft HMS-F-001).
    std::size_t maxNameChars = 100;
    std::size_t maxAddressChars = 200;

    // Date of birth from 01-01-1900 to today (SRS v11 draft 2.5).
    Date earliestDateOfBirth{1900, 1, 1};

    // When the date of birth is not known, the age may be entered; it is
    // stored as an estimated date of birth (1 January of the birth year) and
    // flagged (submitted SRS v1.1 HMS-F-001 "age/DOB"; deviation D-03).
    int maxAgeYears = 125;

    // HMS-F-002 duplicate rule for a shared phone number (deviation D-02).
    //  true  = team decision for Sprint 1: a patient who only shares a phone
    //          number (for example a family member) may be registered as a
    //          different person after staff review.
    //  false = submitted SRS v1.1 AC-HMS-F-002: a matching phone number blocks
    //          a new UHID; staff must use the existing record.
    bool phoneMatchAllowsNewPatient = true;

    // Search results are capped so a short name query on a large store stays
    // readable; the total number of matches is always reported.
    std::size_t maxSearchResults = 50;
};

struct AppPolicy {
    // SRS 2.5: hospital-local time is Asia/Kolkata, UTC+05:30, no DST.
    int utcOffsetMinutes = 330;
    PatientPolicy patient;
};

}  // namespace hms
