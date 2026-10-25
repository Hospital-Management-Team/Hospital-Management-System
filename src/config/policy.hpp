#pragma once
// Build-time policy values that the SRS marks [TEAM DECISION] or that are
// likely to change when the SRS/SAD/STP are revised. Keeping them in one
// place means a document change becomes a one-line code change plus a test
// update. Each feature adds its own policy struct here.
//
// Values that Admin edits at run time (HMS-F-052: cutoff, reminder window,
// lockout cooldown, low-stock threshold) belong in a system_parameter table.

namespace hms {

struct AppPolicy {
    // SRS 2.5: hospital-local time is Asia/Kolkata, UTC+05:30, no DST.
    int utcOffsetMinutes = 330;
};

}  // namespace hms
