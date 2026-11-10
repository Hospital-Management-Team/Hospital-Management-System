// Administrator screens: staff accounts, doctors' departments and weekly hours (HMS-F-020),
// consultation fees, master data (HMS-F-050), reports (HMS-F-051), the audit log (HMS-SR-004)
// and system settings (HMS-F-052).

#include <algorithm>
#include <functional>

#include "appointment/appointment.hpp"
#include "billing/receipt.hpp"
#include "config/settings.hpp"
#include "core/money.hpp"
#include "core/text.hpp"
#include "data/store.hpp"
#include "gui/screens.hpp"
#include "imgui_stdlib.h"
#include "validation/input.hpp"

namespace hms::gui {

namespace {

float tableHeight(std::size_t rows, std::size_t most = 10) { return rowsHeight(rows, most); }

// A drop-down list of names. Returns true when the choice changed.
bool nameCombo(const char* label, const std::vector<std::string>& names, int& index) {
    const std::string preview = index >= 0 && index < static_cast<int>(names.size()) ? names[static_cast<std::size_t>(index)] : "Choose...";
    bool changed = false;
    ImGui::TextDisabled("%s", label);
    ImGui::PushID(label);
    ImGui::SetNextItemWidth(ImGui::GetFontSize() * 16.0F);
    if (ImGui::BeginCombo("##combo", preview.c_str())) {
        for (std::size_t i = 0; i < names.size(); ++i) {
            if (ImGui::Selectable(names[i].c_str(), index == static_cast<int>(i))) {
                index = static_cast<int>(i);
                changed = true;
            }
        }
        ImGui::EndCombo();
    }
    ImGui::PopID();
    return changed;
}

// ---- Staff accounts -------------------------------------------------------------------------------

class StaffScreen final : public Screen {
public:
    void onOpen(Context& ctx) override { load(ctx); }

    void draw(Context& ctx) override {
        heading("Staff accounts");
        notice_.draw();
        if (!rows_.empty() && beginTable("staff", {"Username", "Role", "Status"}, tableHeight(rows_.size(), 8))) {
            for (std::size_t i = 0; i < rows_.size(); ++i) {
                const auto& m = rows_[i];
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::PushID(static_cast<int>(i));
                if (ImGui::Selectable(m.username.c_str(), selected_ == static_cast<int>(i), ImGuiSelectableFlags_SpanAllColumns)) {
                    selected_ = static_cast<int>(i);
                    newPassword1_.clear();
                    newPassword2_.clear();
                }
                ImGui::PopID();
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(m.role.c_str());
                ImGui::TableSetColumnIndex(2);
                ImGui::TextUnformatted(!m.active ? "deactivated" : (m.locked ? "locked" : "active"));
            }
            ImGui::EndTable();
        }
        if (selected_ >= 0 && selected_ < static_cast<int>(rows_.size())) drawSelected(ctx);
        drawCreate(ctx);
    }

private:
    void drawSelected(Context& ctx) {
        const StaffMember m = rows_[static_cast<std::size_t>(selected_)];
        sectionTitle(m.username.c_str());
        if (m.active) {
            if (dangerButton("Deactivate")) act(ctx, [&]() { return ctx.svc.admin.deactivate(ctx.session, m.staffId); }, "is deactivated and can no longer log in");
        } else {
            if (primaryButton("Reactivate")) act(ctx, [&]() { return ctx.svc.admin.reactivate(ctx.session, m.staffId); }, "is active again");
        }
        if (m.locked) {
            ImGui::SameLine();
            if (plainButton("Unlock")) act(ctx, [&]() { return ctx.svc.admin.unlock(ctx.session, m.staffId); }, "is unlocked");
        }
        vspace();
        passwordField("New password", newPassword1_);
        passwordField("Repeat the new password", newPassword2_);
        if (plainButton("Set the new password")) {
            if (newPassword1_ != newPassword2_) {
                notice_.error("Passwords do not match.");
            } else {
                act(ctx, [&]() { return ctx.svc.admin.resetPassword(ctx.session, m.staffId, newPassword1_); }, "has a new password");
                wipe(newPassword1_);
                wipe(newPassword2_);
            }
        }
    }

    void drawCreate(Context& ctx) {
        sectionTitle("Create a staff account");
        textField("Username (no spaces)", user_);
        ImGui::TextDisabled("Role");
        ImGui::RadioButton("Doctor", &role_, 0);
        ImGui::SameLine();
        ImGui::RadioButton("Receptionist", &role_, 1);
        ImGui::SameLine();
        ImGui::RadioButton("Administrator", &role_, 2);
        ImGui::SameLine();
        ImGui::RadioButton("Lab Technician", &role_, 3);
        ImGui::SameLine();
        ImGui::RadioButton("Pharmacist", &role_, 4);
        passwordField("Password (8+ characters with upper-case, lower-case, a digit and a symbol)", pass1_);
        passwordField("Repeat the password", pass2_);
        if (primaryButton("Create the account")) {
            notice_.clear();
            std::string reason;
            if (pass1_ != pass2_) {
                notice_.error("Passwords do not match.");
            } else if (!auth::isPasswordAcceptable(pass1_, reason)) {
                notice_.error(reason);
            } else {
                static const auth::Role kRoles[5] = {auth::Role::Doctor, auth::Role::Receptionist, auth::Role::Administrator,
                                                     auth::Role::LabTechnician, auth::Role::Pharmacist};
                if (auth::createStaffAccount(ctx.svc.db, user_, pass1_, kRoles[role_], ctx.svc.clock, ctx.session.userId)) {
                    notice_.info("Account created for '" + user_ + "'.");
                    user_.clear();
                    wipe(pass1_);
                    wipe(pass2_);
                    load(ctx);
                } else {
                    notice_.error("Could not create the account (empty name, contains spaces, or already used).");
                }
            }
        }
    }

    void act(Context& ctx, const std::function<Result<StaffMember>()>& call, const char* what) {
        notice_.clear();
        if (ctx.blocked(notice_)) return;
        const Result<StaffMember> r = call();
        if (!r) {
            ctx.fail(notice_, r.error());
            return;
        }
        notice_.info(r.value().username + " " + what + ".");
        load(ctx);
    }

    void load(Context& ctx) {
        auto r = ctx.svc.admin.listStaff(ctx.session);
        selected_ = -1;
        if (!r) {
            ctx.fail(notice_, r.error());
            rows_.clear();
            return;
        }
        rows_ = r.value();
    }

    Notice notice_;
    std::vector<StaffMember> rows_;
    int selected_ = -1;
    std::string newPassword1_, newPassword2_;
    std::string user_, pass1_, pass2_;
    int role_ = 0;
};

// ---- Doctors' departments and weekly hours --------------------------------------------------------

class DoctorHoursScreen final : public Screen {
public:
    void onOpen(Context& ctx) override { load(ctx); }

    void draw(Context& ctx) override {
        heading("Doctors' departments and weekly hours");
        notice_.draw();
        if (rows_.empty()) {
            muted("No doctor account exists yet. Create one under Staff accounts.");
            return;
        }
        if (beginTable("doctors", {"Doctor", "Department", "Hours"}, tableHeight(rows_.size(), 8))) {
            for (std::size_t i = 0; i < rows_.size(); ++i) {
                const auto& d = rows_[i];
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::PushID(static_cast<int>(i));
                if (ImGui::Selectable(("Dr " + d.username).c_str(), selected_ == static_cast<int>(i), ImGuiSelectableFlags_SpanAllColumns)) {
                    select(static_cast<int>(i));
                }
                ImGui::PopID();
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(d.hasProfile ? d.department.c_str() : "(not set)");
                ImGui::TableSetColumnIndex(2);
                std::string summary;
                for (const auto& h : d.hours) {
                    if (!summary.empty()) summary += ", ";
                    summary += weekdayName(h.weekday).substr(0, 3) + " " + minuteText(h.startMinute) + "-" + minuteText(h.endMinute);
                }
                ImGui::TextUnformatted(summary.empty() ? "no working hours" : summary.c_str());
            }
            ImGui::EndTable();
        }
        if (selected_ < 0 || selected_ >= static_cast<int>(rows_.size())) return;
        const DoctorSummary d = rows_[static_cast<std::size_t>(selected_)];
        sectionTitle(("Dr " + d.username).c_str());
        textField("Department", department_);
        textField("Specialization (optional)", specialization_);
        muted("One window per weekday, typed as 09:00-13:00, or off.");
        for (int day = 1; day <= 7; ++day) {
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 14.0F);
            ImGui::InputTextWithHint((weekdayName(day) + "##d" + std::to_string(day)).c_str(), "09:00-13:00 or off", &hours_[static_cast<std::size_t>(day - 1)]);
        }
        vspace();
        if (primaryButton("Save")) save(ctx, d);
    }

private:
    void select(int index) {
        selected_ = index;
        const DoctorSummary& d = rows_[static_cast<std::size_t>(index)];
        department_ = d.department;
        specialization_ = d.specialization.value_or("");
        for (int day = 1; day <= 7; ++day) hours_[static_cast<std::size_t>(day - 1)] = hoursText(d, day);
    }

    void save(Context& ctx, const DoctorSummary& d) {
        notice_.clear();
        if (ctx.blocked(notice_)) return;
        DoctorScheduleDraft draft;
        draft.department = department_;
        draft.specialization = specialization_;
        for (int day = 1; day <= 7; ++day) {
            auto h = parseHoursText(hours_[static_cast<std::size_t>(day - 1)], day);
            if (!h) {
                notice_.error(h.error());
                return;
            }
            if (h.value()) draft.hours.push_back(*h.value());
        }
        auto r = ctx.svc.appointments.setDoctorSchedule(ctx.session, d.staffId, draft);
        if (!r) {
            ctx.fail(notice_, r.error());
            return;
        }
        std::string msg = "Saved. The booking screen uses these hours.";
        if (r.value().bookedOutsideHours > 0) {
            msg += " " + std::to_string(r.value().bookedOutsideHours) + " booked appointment(s) fall outside the new hours; reschedule them.";
        }
        const std::int64_t keep = d.staffId;
        load(ctx);
        for (std::size_t i = 0; i < rows_.size(); ++i) {
            if (rows_[i].staffId == keep) select(static_cast<int>(i));
        }
        notice_.info(msg);
    }

    void load(Context& ctx) {
        auto r = ctx.svc.appointments.listDoctors(ctx.session);
        selected_ = -1;
        if (!r) {
            ctx.fail(notice_, r.error());
            rows_.clear();
            return;
        }
        rows_ = r.value();
    }

    Notice notice_;
    std::vector<DoctorSummary> rows_;
    int selected_ = -1;
    std::string department_, specialization_;
    std::string hours_[7];
};

// ---- Consultation fees ----------------------------------------------------------------------------

class FeesScreen final : public Screen {
public:
    void onOpen(Context& ctx) override { load(ctx); }

    void draw(Context& ctx) override {
        heading("Consultation fees");
        notice_.draw();
        if (rows_.empty()) {
            muted("No doctor has a profile yet. Set a doctor's department and hours first.");
            return;
        }
        if (beginTable("fees", {"Doctor", "Department", "Fee"}, tableHeight(rows_.size(), 8))) {
            for (std::size_t i = 0; i < rows_.size(); ++i) {
                const auto& d = rows_[i];
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::PushID(static_cast<int>(i));
                if (ImGui::Selectable(d.username.c_str(), selected_ == static_cast<int>(i), ImGuiSelectableFlags_SpanAllColumns)) {
                    selected_ = static_cast<int>(i);
                    fee_.clear();
                }
                ImGui::PopID();
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(d.department.c_str());
                ImGui::TableSetColumnIndex(2);
                ImGui::TextUnformatted(d.feePaise ? formatRupees(*d.feePaise).c_str() : "not set");
            }
            ImGui::EndTable();
        }
        if (selected_ < 0 || selected_ >= static_cast<int>(rows_.size())) return;
        const DoctorFee d = rows_[static_cast<std::size_t>(selected_)];
        vspace();
        textField(("New consultation fee in rupees for Dr " + d.username + " (for example 500 or 500.50)").c_str(), fee_);
        if (primaryButton("Save the fee")) {
            notice_.clear();
            if (ctx.blocked(notice_)) return;
            auto r = ctx.svc.billing.setConsultationFee(ctx.session, d.staffId, fee_);
            if (!r) {
                ctx.fail(notice_, r.error());
                return;
            }
            load(ctx);
            notice_.info("Fee for Dr " + r.value().username + " is now " + formatRupees(*r.value().feePaise) +
                         ". Bills already made keep the fee they were made with.");
        }
    }

private:
    void load(Context& ctx) {
        auto r = ctx.svc.billing.listDoctorFees(ctx.session);
        selected_ = -1;
        if (!r) {
            ctx.fail(notice_, r.error());
            rows_.clear();
            return;
        }
        rows_ = r.value();
    }

    Notice notice_;
    std::vector<DoctorFee> rows_;
    int selected_ = -1;
    std::string fee_;
};

// ---- Master data: departments, rooms, doctor mapping -------------------------------------------------

class MasterDataScreen final : public Screen {
public:
    void onOpen(Context& ctx) override { load(ctx); }

    void draw(Context& ctx) override {
        heading("Master data");
        notice_.draw();
        if (!ImGui::BeginTabBar("master")) return;
        if (ImGui::BeginTabItem("Departments")) {
            drawDepartments(ctx);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Consultation rooms")) {
            drawRooms(ctx);
            ImGui::EndTabItem();
        }
        if (ImGui::BeginTabItem("Doctor-department mapping")) {
            drawMapping(ctx);
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

private:
    template <typename T>
    void done(Context& ctx, const Result<T>& r, const std::string& message) {
        if (!r) {
            ctx.fail(notice_, r.error());
            return;
        }
        load(ctx);
        notice_.info(message);
    }

    void drawDepartments(Context& ctx) {
        vspace();
        if (departments_.empty()) {
            muted("No department yet. Add one below, or set a doctor's department.");
        } else if (beginTable("departments", {"Name", "Doctors", "Status"}, tableHeight(departments_.size(), 8))) {
            for (std::size_t i = 0; i < departments_.size(); ++i) {
                const auto& d = departments_[i];
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::PushID(static_cast<int>(i));
                if (ImGui::Selectable(d.name.c_str(), department_ == static_cast<int>(i), ImGuiSelectableFlags_SpanAllColumns)) {
                    department_ = static_cast<int>(i);
                    renameTo_ = d.name;
                }
                ImGui::PopID();
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(std::to_string(d.doctors).c_str());
                ImGui::TableSetColumnIndex(2);
                ImGui::TextUnformatted(d.active ? "in use" : "switched off");
            }
            ImGui::EndTable();
        }
        if (department_ >= 0 && department_ < static_cast<int>(departments_.size())) {
            const Department d = departments_[static_cast<std::size_t>(department_)];
            vspace();
            textField("Rename the selected department", renameTo_);
            if (plainButton("Rename")) {
                notice_.clear();
                if (!ctx.blocked(notice_)) done(ctx, ctx.svc.admin.renameDepartment(ctx.session, d.departmentId, renameTo_), "Department renamed.");
            }
            ImGui::SameLine();
            if (plainButton(d.active ? "Switch off" : "Switch on")) {
                notice_.clear();
                if (!ctx.blocked(notice_)) {
                    done(ctx, ctx.svc.admin.setDepartmentActive(ctx.session, d.departmentId, !d.active), "Department updated.");
                }
            }
        }
        sectionTitle("Add a department");
        textField("New department name", newDepartment_);
        if (primaryButton("Add the department")) {
            notice_.clear();
            if (!ctx.blocked(notice_)) {
                auto r = ctx.svc.admin.addDepartment(ctx.session, newDepartment_);
                if (r) newDepartment_.clear();
                done(ctx, r, "Department added.");
            }
        }
    }

    void drawRooms(Context& ctx) {
        vspace();
        std::vector<std::string> names;
        for (const auto& d : departments_) names.push_back(d.name);
        if (rooms_.empty()) {
            muted("No consultation room yet. Add one below.");
        } else if (beginTable("rooms", {"Room", "Department", "Status"}, tableHeight(rooms_.size(), 8))) {
            for (std::size_t i = 0; i < rooms_.size(); ++i) {
                const auto& r = rooms_[i];
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::PushID(static_cast<int>(i));
                if (ImGui::Selectable(r.name.c_str(), room_ == static_cast<int>(i), ImGuiSelectableFlags_SpanAllColumns)) {
                    room_ = static_cast<int>(i);
                    roomName_ = r.name;
                    roomDepartment_ = -1;
                    for (std::size_t k = 0; k < departments_.size(); ++k) {
                        if (departments_[k].name == r.department) roomDepartment_ = static_cast<int>(k);
                    }
                }
                ImGui::PopID();
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(r.department.empty() ? "-" : r.department.c_str());
                ImGui::TableSetColumnIndex(2);
                ImGui::TextUnformatted(r.active ? "in use" : "switched off");
            }
            ImGui::EndTable();
        }
        if (room_ >= 0 && room_ < static_cast<int>(rooms_.size())) {
            const ConsultationRoom r = rooms_[static_cast<std::size_t>(room_)];
            sectionTitle("Change the selected room");
            textField("Room name", roomName_);
            nameCombo("Department", names, roomDepartment_);
            if (plainButton("Save the room")) {
                notice_.clear();
                if (!ctx.blocked(notice_)) {
                    done(ctx, ctx.svc.admin.updateRoom(ctx.session, r.roomId, roomName_, departmentIdAt(roomDepartment_)), "Room saved.");
                }
            }
            ImGui::SameLine();
            if (plainButton(r.active ? "Switch off" : "Switch on")) {
                notice_.clear();
                if (!ctx.blocked(notice_)) done(ctx, ctx.svc.admin.setRoomActive(ctx.session, r.roomId, !r.active), "Room updated.");
            }
        }
        sectionTitle("Add a room");
        textField("New room name", newRoom_);
        nameCombo("Department (optional)", names, newRoomDepartment_);
        if (primaryButton("Add the room")) {
            notice_.clear();
            if (!ctx.blocked(notice_)) {
                auto r = ctx.svc.admin.addRoom(ctx.session, newRoom_, departmentIdAt(newRoomDepartment_));
                if (r) newRoom_.clear();
                done(ctx, r, "Room saved.");
            }
        }
    }

    void drawMapping(Context& ctx) {
        vspace();
        std::vector<std::string> names;
        for (const auto& d : departments_) names.push_back(d.name);
        if (mappings_.empty()) {
            muted("No doctor account exists yet.");
            return;
        }
        if (beginTable("mapping", {"Doctor", "Department"}, tableHeight(mappings_.size(), 8))) {
            for (std::size_t i = 0; i < mappings_.size(); ++i) {
                const auto& m = mappings_[i];
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::PushID(static_cast<int>(i));
                if (ImGui::Selectable(("Dr " + m.username).c_str(), mapping_ == static_cast<int>(i), ImGuiSelectableFlags_SpanAllColumns)) {
                    mapping_ = static_cast<int>(i);
                    mappingDepartment_ = -1;
                }
                ImGui::PopID();
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(m.department.empty() ? "(none)" : m.department.c_str());
            }
            ImGui::EndTable();
        }
        if (mapping_ >= 0 && mapping_ < static_cast<int>(mappings_.size())) {
            const DoctorMapping m = mappings_[static_cast<std::size_t>(mapping_)];
            vspace();
            nameCombo("Move the selected doctor to department", names, mappingDepartment_);
            if (primaryButton("Assign") && mappingDepartment_ >= 0) {
                notice_.clear();
                if (!ctx.blocked(notice_)) {
                    done(ctx, ctx.svc.admin.assignDoctorDepartment(ctx.session, m.staffId, departmentIdAt(mappingDepartment_)),
                         "Dr " + m.username + " is now in " + names[static_cast<std::size_t>(mappingDepartment_)] + ".");
                }
            }
        }
    }

    std::int64_t departmentIdAt(int index) const {
        return index >= 0 && index < static_cast<int>(departments_.size()) ? departments_[static_cast<std::size_t>(index)].departmentId : 0;
    }

    void load(Context& ctx) {
        department_ = room_ = mapping_ = -1;
        auto d = ctx.svc.admin.listDepartments(ctx.session);
        auto r = ctx.svc.admin.listRooms(ctx.session);
        auto m = ctx.svc.admin.listDoctorMappings(ctx.session);
        departments_.clear();
        rooms_.clear();
        mappings_.clear();
        if (!d) {
            ctx.fail(notice_, d.error());
            return;
        }
        if (!r) {
            ctx.fail(notice_, r.error());
            return;
        }
        if (!m) {
            ctx.fail(notice_, m.error());
            return;
        }
        departments_ = d.value();
        rooms_ = r.value();
        mappings_ = m.value();
    }

    Notice notice_;
    std::vector<Department> departments_;
    std::vector<ConsultationRoom> rooms_;
    std::vector<DoctorMapping> mappings_;
    int department_ = -1;
    int room_ = -1;
    int mapping_ = -1;
    int roomDepartment_ = -1;
    int newRoomDepartment_ = -1;
    int mappingDepartment_ = -1;
    std::string renameTo_, newDepartment_, roomName_, newRoom_;
};

// ---- Reports -------------------------------------------------------------------------------------------

class ReportsScreen final : public Screen {
public:
    void onOpen(Context& ctx) override {
        to_ = ctx.today().toDisplay();
        from_ = civilFromDays(daysFromCivil(ctx.today().year, ctx.today().month, ctx.today().day) - 6).toDisplay();
    }

    void draw(Context& ctx) override {
        if (to_.empty()) onOpen(ctx);
        heading("Summary reports");
        notice_.draw();
        dateField("From", from_, ctx.today());
        dateField("To", to_, ctx.today());
        if (primaryButton("Show the report")) load(ctx);
        if (!loaded_) return;

        const HospitalReport& r = report_;
        sectionTitle(("Hospital summary, " + range_).c_str());
        keyValue("Patients", "Registered (all time): " + std::to_string(r.registeredPatients) +
                                 "   Different patients seen or booked: " + std::to_string(r.patientsSeen));
        keyValue("Appointments", "Total " + std::to_string(r.appointments) + "   Booked " + std::to_string(r.booked) + "   Completed " +
                                     std::to_string(r.completed) + "   Cancelled " + std::to_string(r.cancelled) +
                                     "   Consultations recorded " + std::to_string(r.consultations));
        keyValue("Billing", "Bills " + std::to_string(r.billsCount) + ", total " + formatRupees(r.billedPaise));
        std::string modes;
        for (const auto& m : r.byMode) modes += (modes.empty() ? "" : "   ") + m.mode + " " + formatRupees(m.paise);
        keyValue("Payments", "Received " + std::to_string(r.paymentsCount) + ", total " + formatRupees(r.collectedPaise) +
                                 (modes.empty() ? "" : "   (" + modes + ")"));
        keyValue("Unpaid", formatRupees(r.outstandingPaise) + " still unpaid on those bills");

        if (!r.days.empty()) {
            sectionTitle("Day by day (days with appointments)");
            if (beginTable("days", {"Date", "Patients", "Booked", "Done", "Cancelled"}, tableHeight(r.days.size(), 8))) {
                for (const auto& d : r.days) {
                    ImGui::TableNextRow();
                    const auto date = Date::fromIso(d.date);
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextUnformatted(date ? date->toDisplay().c_str() : d.date.c_str());
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextUnformatted(std::to_string(d.patientsSeen).c_str());
                    ImGui::TableSetColumnIndex(2);
                    ImGui::TextUnformatted(std::to_string(d.booked).c_str());
                    ImGui::TableSetColumnIndex(3);
                    ImGui::TextUnformatted(std::to_string(d.completed).c_str());
                    ImGui::TableSetColumnIndex(4);
                    ImGui::TextUnformatted(std::to_string(d.cancelled).c_str());
                }
                ImGui::EndTable();
            }
        }
        if (!r.doctors.empty()) {
            sectionTitle("Doctor-wise appointments");
            if (beginTable("doctorwise", {"Doctor", "Department", "Total", "Booked", "Done", "Cancelled"}, tableHeight(r.doctors.size(), 8))) {
                for (const auto& d : r.doctors) {
                    ImGui::TableNextRow();
                    ImGui::TableSetColumnIndex(0);
                    ImGui::TextUnformatted(("Dr " + d.doctor).c_str());
                    ImGui::TableSetColumnIndex(1);
                    ImGui::TextUnformatted(d.department.c_str());
                    ImGui::TableSetColumnIndex(2);
                    ImGui::TextUnformatted(std::to_string(d.total()).c_str());
                    ImGui::TableSetColumnIndex(3);
                    ImGui::TextUnformatted(std::to_string(d.booked).c_str());
                    ImGui::TableSetColumnIndex(4);
                    ImGui::TextUnformatted(std::to_string(d.completed).c_str());
                    ImGui::TableSetColumnIndex(5);
                    ImGui::TextUnformatted(std::to_string(d.cancelled).c_str());
                }
                ImGui::EndTable();
            }
        }
        sectionTitle("Staff accounts");
        for (const auto& s : r.staff) {
            keyValue(s.role.c_str(), std::to_string(s.active) + " active" + (s.deactivated > 0 ? ", " + std::to_string(s.deactivated) + " deactivated" : std::string()));
        }
    }

private:
    void load(Context& ctx) {
        notice_.clear();
        loaded_ = false;
        auto from = input::parseDate(from_, "From");
        if (!from) {
            notice_.error(from.error());
            return;
        }
        auto to = input::parseDate(to_, "To");
        if (!to) {
            notice_.error(to.error());
            return;
        }
        auto r = ctx.svc.admin.report(ctx.session, from.value(), to.value());
        if (!r) {
            ctx.fail(notice_, r.error());
            return;
        }
        report_ = r.value();
        range_ = from.value().toDisplay() + " to " + to.value().toDisplay();
        loaded_ = true;
    }

    Notice notice_;
    std::string from_, to_, range_;
    HospitalReport report_;
    bool loaded_ = false;
};

// ---- Audit log -------------------------------------------------------------------------------------------

class AuditLogScreen final : public Screen {
public:
    void onOpen(Context& ctx) override { load(ctx); }

    void draw(Context& ctx) override {
        heading("Audit log");
        muted("Newest first. This screen is read only: no entry can be changed or deleted.");
        vspace();
        notice_.draw();
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 6.0F);
        ImGui::InputInt("Entries to show", &count_, 0, 0);
        ImGui::SameLine();
        if (primaryButton("Show")) load(ctx);
        vspace();
        if (rows_.empty()) return;
        const int offset = ctx.svc.audit.policy().utcOffsetMinutes;
        ImGui::TextDisabled("%zu entries shown.", rows_.size());
        if (!ImGui::BeginTable("audit", 6, ImGuiTableFlags_RowBg | ImGuiTableFlags_Borders | ImGuiTableFlags_Resizable |
                                               ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp)) {
            return;
        }
        ImGui::TableSetupColumn("Time", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 9.5F);
        ImGui::TableSetupColumn("User", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 7.0F);
        ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 18.0F);
        ImGui::TableSetupColumn("Record", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 8.0F);
        ImGui::TableSetupColumn("Outcome", ImGuiTableColumnFlags_WidthFixed, ImGui::GetFontSize() * 6.0F);
        ImGui::TableSetupColumn("Changed fields / reason");
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableHeadersRow();
        for (const auto& e : rows_) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(localDateTimeText(e.recordedAt, offset).c_str());
            ImGui::TableSetColumnIndex(1);
            ImGui::TextUnformatted(e.userId.c_str());
            ImGui::TableSetColumnIndex(2);
            ImGui::TextUnformatted(e.action.c_str());
            ImGui::TableSetColumnIndex(3);
            ImGui::TextUnformatted(e.recordId.value_or("").c_str());
            ImGui::TableSetColumnIndex(4);
            ImGui::TextUnformatted(e.outcome.c_str());
            ImGui::TableSetColumnIndex(5);
            std::string detail;
            if (e.changedFields && !e.changedFields->empty()) detail += "fields: " + *e.changedFields;
            if (e.reasonCode && !e.reasonCode->empty()) detail += (detail.empty() ? "" : "   ") + std::string("reason: ") + *e.reasonCode;
            ImGui::TextUnformatted(detail.c_str());
        }
        ImGui::EndTable();
    }

private:
    void load(Context& ctx) {
        notice_.clear();
        const std::size_t most = ctx.svc.policy.auditView.maxEntries;
        count_ = std::max(1, std::min(count_, static_cast<int>(most)));
        auto r = ctx.svc.audit.recentEntries(ctx.session, static_cast<std::size_t>(count_));
        if (!r) {
            ctx.fail(notice_, r.error());
            rows_.clear();
            return;
        }
        rows_ = r.value();
    }

    Notice notice_;
    int count_ = 50;
    std::vector<AuditEntry> rows_;
};

// ---- System settings ---------------------------------------------------------------------------------------

class SettingsScreen final : public Screen {
public:
    void onOpen(Context& ctx) override { load(ctx); }

    void draw(Context& ctx) override {
        heading("System settings");
        muted("A change applies from the next operation, without a restart, and is recorded in the audit log.");
        vspace();
        notice_.draw();
        for (std::size_t i = 0; i < rows_.size(); ++i) {
            const auto& s = rows_[i];
            ImGui::PushID(static_cast<int>(i));
            const std::string caption = std::string(s.info.label) + " (" + s.info.unit + ", " + std::to_string(s.info.min) + " to " +
                                        std::to_string(s.info.max) + ")";
            textField(caption.c_str(), texts_[i]);
            muted(("Now: " + std::to_string(s.value) + " " + s.info.unit + "   Built-in default: " + std::to_string(s.defaultValue) +
                   (s.changed() ? "   (changed)" : ""))
                      .c_str());
            if (primaryButton("Save")) {
                notice_.clear();
                if (!ctx.blocked(notice_)) {
                    auto r = ctx.svc.admin.updateSetting(ctx.session, s.info.id, texts_[i]);
                    if (!r) {
                        ctx.fail(notice_, r.error());
                    } else {
                        const std::string message = std::string("Saved: ") + r.value().info.key + " is now " + std::to_string(r.value().value) + " " +
                                                    r.value().info.unit + ".";
                        ImGui::PopID();
                        load(ctx);
                        notice_.info(message);
                        return;
                    }
                }
            }
            vspace();
            ImGui::PopID();
        }
    }

private:
    void load(Context& ctx) {
        auto r = ctx.svc.admin.listSettings(ctx.session);
        rows_.clear();
        texts_.clear();
        if (!r) {
            ctx.fail(notice_, r.error());
            return;
        }
        rows_ = r.value();
        for (const auto& s : rows_) texts_.push_back(std::to_string(s.value));
    }

    Notice notice_;
    std::vector<SettingValue> rows_;
    std::vector<std::string> texts_;
};

}  // namespace

std::unique_ptr<Screen> makeStaffScreen() { return std::make_unique<StaffScreen>(); }
std::unique_ptr<Screen> makeDoctorHoursScreen() { return std::make_unique<DoctorHoursScreen>(); }
std::unique_ptr<Screen> makeFeesScreen() { return std::make_unique<FeesScreen>(); }
std::unique_ptr<Screen> makeMasterDataScreen() { return std::make_unique<MasterDataScreen>(); }
std::unique_ptr<Screen> makeReportsScreen() { return std::make_unique<ReportsScreen>(); }
std::unique_ptr<Screen> makeAuditLogScreen() { return std::make_unique<AuditLogScreen>(); }
std::unique_ptr<Screen> makeSettingsScreen() { return std::make_unique<SettingsScreen>(); }

}  // namespace hms::gui
