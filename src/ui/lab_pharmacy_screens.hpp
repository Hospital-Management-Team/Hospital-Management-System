#pragma once
// Menus for the Lab Technician and the Pharmacist (issue #8). Like the rest of
// the console UI they hold no business rule: they read input, call one service
// function and show the result. The Doctor's lab-results screen is a
// ConsoleApp method (console_app.hpp).

#include "auth/session.hpp"
#include "lab/lab_service.hpp"
#include "pharmacy/pharmacy_service.hpp"
#include "ui/console_io.hpp"

namespace hms {

// Run until the user logs out (option 0) or the input ends.
void labMenu(ConsoleIO& io, LabService& lab, const Session& session);
void pharmacyMenu(ConsoleIO& io, PharmacyService& pharmacy, const Session& session);

}  // namespace hms
