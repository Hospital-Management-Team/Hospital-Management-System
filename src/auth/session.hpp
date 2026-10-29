#pragma once
// The session of the logged-in user. Every business function receives it so
// the caller's role can be checked (SAD v1.10 section 4.4).
//
// main.cpp creates it after a successful staff login (issue #3); the role
// comes from the stored account. `simulated` was used before login existed
// (deviation log D-01, closed) and is always false now.

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
