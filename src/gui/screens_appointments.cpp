// Appointment screens: book (HMS-F-010, F-011), reschedule or cancel (HMS-F-012) and the
// reminder list (HMS-F-013).

#include "appointment/appointment.hpp"
#include "gui/patient_picker.hpp"
#include "gui/screens.hpp"
#include "imgui_stdlib.h"
#include "patient/uhid.hpp"
#include "validation/input.hpp"

namespace hms::gui {

namespace {

// Free slots as a grid of buttons. Returns the chosen index (or the old one).
int slotGrid(const std::vector<LocalSlot>& slots, int selected) {
    ImGui::TextDisabled("Free slots");
    for (std::size_t i = 0; i < slots.size(); ++i) {
        const int index = static_cast<int>(i);
        if (i % 8 != 0) ImGui::SameLine();
        ImGui::PushID(index);
        const bool isSelected = index == selected;
        const float width = ImGui::GetFontSize() * 4.0F;
        const bool pressed = isSelected ? primaryButton(slots[i].timeText().c_str(), width)
                                        : plainButton(slots[i].timeText().c_str(), width);
        if (pressed) selected = index;
        ImGui::PopID();
    }
    return selected;
}

// ---- Book an appointment ------------------------------------------------------------------

class BookScreen final : public Screen {
public:
    void onOpen(Context& ctx) override {
        if (date_.empty()) date_ = ctx.today().toDisplay();
    }

    void draw(Context& ctx) override {
        if (date_.empty()) date_ = ctx.today().toDisplay();
        heading("Book an appointment");
        notice_.draw();
        picker_.draw(ctx);
        vspace();
        dateField("Date", date_, ctx.today());
        if (primaryButton("Show doctors working that day")) showDoctors(ctx);
        if (!shown_) return;

        sectionTitle(("Doctors working on " + dayText(shownDate_)).c_str());
        if (days_.empty()) {
            muted("No doctor works on this day. Choose another date.");
            return;
        }
        const float height = rowsHeight(days_.size(), 50);
        if (beginTable("doctors", {"Doctor", "Department", "Hours", "Free slots"}, height)) {
            for (std::size_t i = 0; i < days_.size(); ++i) {
                const auto& d = days_[i];
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::PushID(static_cast<int>(i));
                if (ImGui::Selectable(("Dr " + d.doctor.username).c_str(), doctor_ == static_cast<int>(i),
                                      ImGuiSelectableFlags_SpanAllColumns)) {
                    doctor_ = static_cast<int>(i);
                    slot_ = -1;
                }
                ImGui::PopID();
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(d.doctor.department.c_str());
                ImGui::TableSetColumnIndex(2);
                ImGui::TextUnformatted((minuteText(d.hours.startMinute) + "-" + minuteText(d.hours.endMinute)).c_str());
                ImGui::TableSetColumnIndex(3);
                ImGui::TextUnformatted(std::to_string(d.freeSlots.size()).c_str());
            }
            ImGui::EndTable();
        }
        if (doctor_ < 0) {
            muted("Choose a doctor from the list.");
            return;
        }
        const DoctorDay& day = days_[static_cast<std::size_t>(doctor_)];
        if (day.freeSlots.empty()) {
            muted("This doctor has no free slots left on this day. Choose another date or doctor.");
            return;
        }
        vspace();
        slot_ = slotGrid(day.freeSlots, slot_);
        if (slot_ < 0) return;

        const LocalSlot chosen = day.freeSlots[static_cast<std::size_t>(slot_)];
        vspace();
        if (picker_.patient()) {
            ImGui::TextWrapped("Book %s (%s) with Dr %s (%s) on %s at %s?", picker_.patient()->fullName.c_str(),
                               formatUhid(picker_.uhid()).c_str(), day.doctor.username.c_str(), day.doctor.department.c_str(),
                               dayText(chosen.date).c_str(), chosen.timeText().c_str());
        } else {
            muted("Look up the patient first.");
        }
        ImGui::BeginDisabled(!picker_.patient());
        if (primaryButton("Confirm the booking")) book(ctx, day, chosen);
        ImGui::EndDisabled();
    }

private:
    void showDoctors(Context& ctx) {
        notice_.clear();
        auto date = input::parseDate(date_, "Date");
        if (!date) {
            notice_.error(date.error());
            shown_ = false;
            return;
        }
        auto r = ctx.svc.appointments.doctorsOn(ctx.session, date.value());
        if (!r) {
            ctx.fail(notice_, r.error());
            shown_ = false;
            return;
        }
        days_ = r.value();
        shownDate_ = date.value();
        shown_ = true;
        doctor_ = -1;
        slot_ = -1;
    }

    void book(Context& ctx, const DoctorDay& day, const LocalSlot& chosen) {
        notice_.clear();
        if (ctx.blocked(notice_)) return;
        auto r = ctx.svc.appointments.book(ctx.session, picker_.uhid(), day.doctor.staffId, chosen);
        if (!r) {
            ctx.fail(notice_, r.error());
            // The slot was taken meanwhile: show the doctors and free slots again, and keep the
            // refusal on screen (the refresh clears the notice).
            if (r.error().kind == ErrorKind::Conflict) {
                const Notice refusal = notice_;
                showDoctors(ctx);
                notice_ = refusal;
            }
            return;
        }
        const AppointmentRecord a = r.value();
        showDoctors(ctx);  // the booked slot is no longer free; this refresh also clears the notice
        notice_.info("Appointment booked: " + formatAppointmentId(a.id) + "\n" + dayText(a.start.date) + " at " +
                     a.start.timeText() + ", Dr " + a.doctorUsername + " (" + a.department +
                     ")\nGive the patient this appointment number.");
    }

    PatientPicker picker_;
    std::string date_;
    Notice notice_;
    bool shown_ = false;
    Date shownDate_;
    std::vector<DoctorDay> days_;
    int doctor_ = -1;
    int slot_ = -1;
};

// ---- Reschedule or cancel -----------------------------------------------------------------

class ChangeAppointmentScreen final : public Screen {
public:
    void draw(Context& ctx) override {
        heading("Reschedule or cancel an appointment");
        muted(("Changes close " + std::to_string(ctx.svc.appointments.cutoffMinutes()) +
               " minutes before the appointment starts.")
                  .c_str());
        notice_.draw();
        if (picker_.draw(ctx) || (picker_.uhid() != loadedFor_ && picker_.patient())) load(ctx);
        if (!picker_.patient()) return;

        sectionTitle("Upcoming appointments");
        if (list_.empty()) {
            muted("This patient has no upcoming appointments.");
            return;
        }
        const float height = rowsHeight(list_.size(), 50);
        if (beginTable("upcoming", {"Appointment", "When", "Doctor", "Department"}, height)) {
            for (std::size_t i = 0; i < list_.size(); ++i) {
                const auto& a = list_[i];
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::PushID(static_cast<int>(i));
                if (ImGui::Selectable(formatAppointmentId(a.id).c_str(), selected_ == static_cast<int>(i),
                                      ImGuiSelectableFlags_SpanAllColumns)) {
                    selected_ = static_cast<int>(i);
                    resetAction();
                }
                ImGui::PopID();
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted((dayText(a.start.date) + " " + a.start.timeText()).c_str());
                ImGui::TableSetColumnIndex(2);
                ImGui::TextUnformatted(("Dr " + a.doctorUsername).c_str());
                ImGui::TableSetColumnIndex(3);
                ImGui::TextUnformatted(a.department.c_str());
            }
            ImGui::EndTable();
        }
        if (selected_ < 0 || selected_ >= static_cast<int>(list_.size())) return;
        const AppointmentRecord a = list_[static_cast<std::size_t>(selected_)];

        vspace();
        if (action_ == Action::None) {
            if (primaryButton("Reschedule")) {
                action_ = Action::Reschedule;
                newDate_ = a.start.date.toDisplay();
            }
            ImGui::SameLine();
            if (dangerButton("Cancel the appointment")) action_ = Action::ConfirmCancel;
        } else if (action_ == Action::ConfirmCancel) {
            ImGui::TextWrapped("Cancel %s (%s at %s, Dr %s)?", formatAppointmentId(a.id).c_str(), dayText(a.start.date).c_str(),
                               a.start.timeText().c_str(), a.doctorUsername.c_str());
            if (dangerButton("Yes, cancel it")) cancel(ctx, a);
            ImGui::SameLine();
            if (plainButton("No, keep it")) resetAction();
        } else {
            drawReschedule(ctx, a);
        }
    }

private:
    enum class Action { None, ConfirmCancel, Reschedule };

    void resetAction() {
        action_ = Action::None;
        slots_.clear();
        slotsShown_ = false;
        slot_ = -1;
    }

    void load(Context& ctx) {
        loadedFor_ = picker_.uhid();
        selected_ = -1;
        resetAction();
        list_.clear();
        auto r = ctx.svc.appointments.upcomingForPatient(ctx.session, picker_.uhid());
        if (!r) {
            ctx.fail(notice_, r.error());
            return;
        }
        list_ = r.value();
    }

    void cancel(Context& ctx, const AppointmentRecord& a) {
        notice_.clear();
        resetAction();
        if (ctx.blocked(notice_)) return;
        auto r = ctx.svc.appointments.cancel(ctx.session, a.id);
        if (!r) {
            ctx.fail(notice_, r.error());
            return;
        }
        notice_.info(formatAppointmentId(a.id) + " is cancelled. The slot is free for other patients.");
        load(ctx);
    }

    void drawReschedule(Context& ctx, const AppointmentRecord& a) {
        dateField("New date", newDate_, ctx.today());
        if (primaryButton("Show free slots")) {
            notice_.clear();
            auto date = input::parseDate(newDate_, "Date");
            if (!date) {
                notice_.error(date.error());
            } else {
                auto r = ctx.svc.appointments.freeSlots(ctx.session, a.doctorStaffId, date.value());
                if (!r) {
                    ctx.fail(notice_, r.error());
                } else {
                    slots_ = r.value();
                    slotsShown_ = true;
                    slot_ = -1;
                }
            }
        }
        ImGui::SameLine();
        if (plainButton("Back")) resetAction();
        if (!slotsShown_) return;
        if (slots_.empty()) {
            muted("This doctor has no free slots on that day. Choose another date.");
            return;
        }
        vspace();
        slot_ = slotGrid(slots_, slot_);
        if (slot_ < 0) return;
        const LocalSlot chosen = slots_[static_cast<std::size_t>(slot_)];
        vspace();
        ImGui::TextWrapped("Move %s from %s %s to %s %s with Dr %s?", formatAppointmentId(a.id).c_str(),
                           dayText(a.start.date).c_str(), a.start.timeText().c_str(), dayText(chosen.date).c_str(),
                           chosen.timeText().c_str(), a.doctorUsername.c_str());
        if (primaryButton("Confirm the new time")) {
            notice_.clear();
            if (ctx.blocked(notice_)) return;
            auto r = ctx.svc.appointments.reschedule(ctx.session, a.id, chosen);
            if (!r) {
                ctx.fail(notice_, r.error());
                return;
            }
            notice_.info("Rescheduled. New appointment: " + formatAppointmentId(r.value().id) + ", " +
                         dayText(r.value().start.date) + " at " + r.value().start.timeText() + ".\n" +
                         formatAppointmentId(a.id) + " is cancelled and its slot is free again.");
            load(ctx);
        }
    }

    PatientPicker picker_;
    Notice notice_;
    std::int64_t loadedFor_ = 0;
    std::vector<AppointmentRecord> list_;
    int selected_ = -1;
    Action action_ = Action::None;
    std::string newDate_;
    std::vector<LocalSlot> slots_;
    bool slotsShown_ = false;
    int slot_ = -1;
};

// ---- Reminders ----------------------------------------------------------------------------

class RemindersScreen final : public Screen {
public:
    void draw(Context& ctx) override {
        heading("Appointment reminders");
        muted("Booked appointments starting in the next 48 hours. A reminder entry is recorded the first time "
              "each one is listed (NEW). Run this once a day and contact the NEW patients.");
        vspace();
        notice_.draw();
        if (primaryButton("List the reminders")) {
            notice_.clear();
            if (!ctx.blocked(notice_)) load(ctx);
        }
        if (!loaded_) return;
        if (items_.empty()) {
            vspace();
            muted("No booked appointments in this period.");
            return;
        }
        vspace();
        const float height = rowsHeight(items_.size(), 50);
        if (beginTable("reminders", {"When", "Appointment", "Patient", "Mobile", "Doctor", "Reminder"}, height)) {
            for (const auto& row : items_) {
                ImGui::TableNextRow();
                ImGui::TableSetColumnIndex(0);
                ImGui::TextUnformatted(row.when.c_str());
                ImGui::TableSetColumnIndex(1);
                ImGui::TextUnformatted(row.appointment.c_str());
                ImGui::TableSetColumnIndex(2);
                ImGui::TextUnformatted(row.patient.c_str());
                ImGui::TableSetColumnIndex(3);
                ImGui::TextUnformatted(row.mobile.c_str());
                ImGui::TableSetColumnIndex(4);
                ImGui::TextUnformatted(row.doctor.c_str());
                ImGui::TableSetColumnIndex(5);
                ImGui::TextUnformatted(row.isNew ? "NEW" : "listed before");
            }
            ImGui::EndTable();
        }
    }

private:
    struct Row {
        std::string when, appointment, patient, mobile, doctor;
        bool isNew = false;
    };

    void load(Context& ctx) {
        loaded_ = false;
        items_.clear();
        auto r = ctx.svc.appointments.remindersDue(ctx.session);
        if (!r) {
            ctx.fail(notice_, r.error());
            return;
        }
        int fresh = 0;
        for (const auto& item : r.value()) {
            const auto& a = item.appointment;
            Row row;
            row.when = dayText(a.start.date) + " " + a.start.timeText();
            row.appointment = formatAppointmentId(a.id);
            row.patient = a.patientName + " " + formatUhid(a.uhid);
            row.mobile = "-";
            if (auto p = ctx.svc.patients.getPatient(ctx.session, a.uhid)) row.mobile = phoneCell(p.value());
            row.doctor = "Dr " + a.doctorUsername;
            row.isNew = item.newlyGenerated;
            if (row.isNew) ++fresh;
            items_.push_back(std::move(row));
        }
        loaded_ = true;
        notice_.info(std::to_string(fresh) + (fresh == 1 ? " new reminder" : " new reminders") +
                     " recorded. Contact the patients marked NEW.");
    }

    Notice notice_;
    bool loaded_ = false;
    std::vector<Row> items_;
};

}  // namespace

std::unique_ptr<Screen> makeBookScreen() { return std::make_unique<BookScreen>(); }
std::unique_ptr<Screen> makeChangeAppointmentScreen() { return std::make_unique<ChangeAppointmentScreen>(); }
std::unique_ptr<Screen> makeRemindersScreen() { return std::make_unique<RemindersScreen>(); }

}  // namespace hms::gui
