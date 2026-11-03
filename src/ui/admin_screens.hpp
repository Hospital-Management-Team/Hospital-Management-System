#pragma once
// Admin screens (issue #9): staff accounts (HMS-F-023) and master data (HMS-F-050) and summary reports (HMS-F-051).
// They hold no business rules: AdminService checks the role and does the work.

#include <functional>
#include <string>

#include "admin/admin_service.hpp"
#include "auth/session.hpp"
#include "ui/console_io.hpp"

namespace hms {

// Reads a password without echo; false at end of input. main.cpp passes the real reader.
using PasswordReader = std::function<bool(const std::string& prompt, std::string& out)>;

void staffAccountsScreen(ConsoleIO& io, AdminService& admin, const Session& session, const PasswordReader& readPassword);
void masterDataScreen(ConsoleIO& io, AdminService& admin, const Session& session);
void reportsScreen(ConsoleIO& io, AdminService& admin, const Session& session);

}  // namespace hms
