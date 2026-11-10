#pragma once
// Admin-editable system parameters (SRS HMS-F-052): the appointment change cutoff (HMS-F-012), the
// failed-login lockout cooldown (HMS-SR-003) and the low-stock alert threshold (HMS-F-041).
//
// A value saved by the Admin is stored in the system_parameter table and read again at every use,
// so a change applies to the next operation without a rebuild or a restart (AC-HMS-F-052). A value
// that was never changed is the build-time default from config/policy.hpp.

#include <cstdint>
#include <vector>

#include "config/policy.hpp"
#include "data/database.hpp"

namespace hms {

enum class SettingId { AppointmentCutoffMinutes, LockoutMinutes, LowStockThreshold };

struct SettingInfo {
    SettingId id;
    const char* key;    // the name stored in system_parameter and written to the audit log
    const char* label;  // shown to the Admin
    const char* unit;   // "minutes", "units"
    std::int64_t min;
    std::int64_t max;
};

// All settings, in the order the Admin sees them.
const std::vector<SettingInfo>& settingInfos();
const SettingInfo& settingInfo(SettingId id);

// The build-time default of a setting.
std::int64_t settingDefault(SettingId id, const AppPolicy& defaults);

class Settings {
public:
    Settings(Database& db, AppPolicy defaults) : db_(db), defaults_(defaults) {}

    // The saved value, or the default when none was saved (or the store cannot be read: the
    // build-time default is the safe answer then).
    std::int64_t get(SettingId id) const;

    int cutoffMinutes() const { return static_cast<int>(get(SettingId::AppointmentCutoffMinutes)); }
    int lockoutMinutes() const { return static_cast<int>(get(SettingId::LockoutMinutes)); }
    std::int64_t lowStockThreshold() const { return get(SettingId::LowStockThreshold); }

    const AppPolicy& defaults() const { return defaults_; }

private:
    Database& db_;
    AppPolicy defaults_;
};

}  // namespace hms
