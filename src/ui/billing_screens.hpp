#pragma once
// Admin screen for the consultation fee (HMS-F-030, issue #7). Holds no
// business rules: BillingService checks the role, validates and audits.

#include "auth/session.hpp"
#include "billing/billing_service.hpp"
#include "ui/console_io.hpp"

namespace hms {

void consultationFeeScreen(ConsoleIO& io, BillingService& billing, const Session& session);

}  // namespace hms
