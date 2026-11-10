#include "config/settings.hpp"

namespace hms {

const std::vector<SettingInfo>& settingInfos() {
    static const std::vector<SettingInfo> kAll = {
        {SettingId::AppointmentCutoffMinutes, "appointment_cutoff_minutes",
         "Appointment change cutoff: reschedule or cancel up to this long before the start", "minutes", 0, 1440},
        {SettingId::LockoutMinutes, "lockout_minutes",
         "Login lockout cooldown: how long an account stays locked after repeated failures", "minutes", 1, 1440},
        {SettingId::LowStockThreshold, "low_stock_threshold",
         "Low-stock alert: a medicine below this many units is flagged", "units", 0, 100000},
    };
    return kAll;
}

const SettingInfo& settingInfo(SettingId id) {
    for (const auto& s : settingInfos()) {
        if (s.id == id) return s;
    }
    return settingInfos().front();
}

std::int64_t settingDefault(SettingId id, const AppPolicy& defaults) {
    switch (id) {
        case SettingId::AppointmentCutoffMinutes: return defaults.appointment.cutoffMinutes;
        case SettingId::LockoutMinutes: return defaults.auth.lockoutMinutes;
        case SettingId::LowStockThreshold: return defaults.labPharmacy.lowStockThreshold;
    }
    return 0;
}

std::int64_t Settings::get(SettingId id) const {
    try {
        auto st = db_.prepare("SELECT value FROM system_parameter WHERE name = ?");
        st.bind(1, std::string(settingInfo(id).key));
        if (st.step()) return st.getInt64(0);
    } catch (const DbError&) {
        // Fall back to the build-time default.
    }
    return settingDefault(id, defaults_);
}

}  // namespace hms
