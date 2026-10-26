#pragma once
// The session of the logged-in user. Every business function receives it so
// the caller's role can be checked (SAD v1.10 section 4.4).
//
// There is no login yet (issue #3). The console creates a clearly marked
// simulated session instead (docs/deviation-log.md, D-01). When login is
// implemented, only the code that CREATES the Session changes; the business
// modules already take it and already check permissions.

#include <string>

namespace hms {

enum class Role { Receptionist, Doctor, LabTechnician, Pharmacist, Admin };

std::string to_string(Role role);

struct Session {
    std::string userId;       // staff ID once StaffUser exists; audit user_id
    std::string displayName;  // shown in menus
    Role role = Role::Receptionist;
    bool simulated = false;   // true for the stand-in session used until login exists
};

}  // namespace hms
