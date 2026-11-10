#include "gui/context.hpp"

namespace hms::gui {

Services::Services(Database& database, const Clock& clk, const AppPolicy& pol)
    : db(database),
      clock(clk),
      policy(pol),
      patients(database, clk, pol),
      appointments(database, clk, pol),
      consultations(database, clk, pol),
      billing(database, clk, pol),
      records(database, clk, pol),
      audit(database, clk, pol),
      admin(database, clk, pol),
      lab(database, clk, pol),
      pharmacy(database, clk, pol) {}

void Context::fail(Notice& notice, const Error& e) {
    if (e.kind == ErrorKind::OutcomeUnknown) {
        changesBlocked = true;
        notice.error(e.describe() + "\nNo further changes can be made in this session. Please restart the application.");
        return;
    }
    notice.error(e);
}

bool Context::blocked(Notice& notice) const {
    if (!changesBlocked) return false;
    notice.error("Changes are blocked until the application is restarted (an earlier save could not be confirmed).");
    return true;
}

}  // namespace hms::gui
