#include "gui/gui_app.hpp"

#include <algorithm>
#include <cfloat>
#include <filesystem>
#include <system_error>
#include <utility>

#include "config/policy.hpp"
#include "config/settings.hpp"
#include "imgui_stdlib.h"

namespace hms::gui {

namespace {

Role toSessionRole(auth::Role r) {
    switch (r) {
        case auth::Role::Administrator: return Role::Admin;
        case auth::Role::Doctor: return Role::Doctor;
        case auth::Role::Receptionist: return Role::Receptionist;
        case auth::Role::LabTechnician: return Role::LabTechnician;
        case auth::Role::Pharmacist: return Role::Pharmacist;
    }
    return Role::Receptionist;
}

const char* roleName(auth::Role r) {
    switch (r) {
        case auth::Role::Administrator: return "Administrator";
        case auth::Role::Doctor: return "Doctor";
        case auth::Role::Receptionist: return "Receptionist";
        case auth::Role::LabTechnician: return "Lab Technician";
        case auth::Role::Pharmacist: return "Pharmacist";
    }
    return "";
}

constexpr ImVec4 kSidebar{0.08F, 0.20F, 0.24F, 1.0F};

}  // namespace

std::vector<NavEntry> navigationFor(auth::Role role) {
    std::vector<NavEntry> nav;
    auto add = [&nav](const char* label, std::unique_ptr<Screen> screen) {
        nav.push_back(NavEntry{label, std::move(screen)});
    };
    switch (role) {
        case auth::Role::Receptionist:
            add("Register patient", makeRegisterScreen());
            add("Patients", makePatientsScreen());
            add("Book appointment", makeBookScreen());
            add("Change appointment", makeChangeAppointmentScreen());
            add("Reminders", makeRemindersScreen());
            add("Bill a visit", makeBillVisitScreen());
            add("Payments", makePaymentScreen());
            add("Bills and balances", makeBillsScreen());
            add("Receipts", makeReceiptScreen());
            break;
        case auth::Role::Doctor:
            add("My appointments", makeMyAppointmentsScreen());
            add("Record consultation", makeRecordConsultationScreen());
            add("Patients", makePatientsScreen());
            add("Past consultations", makePastConsultationsScreen());
            add("Medical history", makeMedicalHistoryScreen());
            add("Lab results", makeLabResultsScreen());
            add("My weekly hours", makeMyHoursScreen());
            break;
        case auth::Role::LabTechnician:
            add("Tests waiting", makeLabPendingScreen());
            add("Tests of a patient", makeLabPatientScreen());
            break;
        case auth::Role::Pharmacist:
            add("Medicine stock", makeStockScreen());
            add("Dispense", makeDispenseScreen());
            add("Prescriptions of a patient", makePatientPrescriptionsScreen());
            break;
        case auth::Role::Administrator:
            add("Staff accounts", makeStaffScreen());
            add("Doctor hours", makeDoctorHoursScreen());
            add("Consultation fees", makeFeesScreen());
            add("Master data", makeMasterDataScreen());
            add("Reports", makeReportsScreen());
            add("Audit log", makeAuditLogScreen());
            add("System settings", makeSettingsScreen());
            break;
    }
    return nav;
}

App::App(StartupOptions options, const Clock* clock)
    : options_(std::move(options)), clock_(clock != nullptr ? clock : &systemClock_) {
    begin();
}

App::~App() {
    wipe(pass1_);
    wipe(pass2_);
    wipe(adminPass1_);
    wipe(adminPass2_);
    wipe(loginPass_);
}

std::string App::activeScreen() const {
    if (phase_ != Phase::Main || nav_.empty()) return {};
    return nav_[static_cast<std::size_t>(active_)].label;
}

// ---- Start-up ------------------------------------------------------------------------------

void App::fatal(std::string message) {
    fatal_ = std::move(message);
    phase_ = Phase::Fatal;
}

void App::begin() {
    canonical_ = canonicalStorePath(options_.storePath);
    // A missing store is created only after confirmation (HMS-F-053 rule 1): nothing is created
    // on disk, not even its folder or the lock file, until the answer is Yes.
    if (!storeFileExists(canonical_)) {
        phase_ = Phase::ConfirmNewStore;
        return;
    }
    prepareStore();
}

void App::prepareStore() {
    namespace fs = std::filesystem;
    std::string warnings;

    // A folder the application creates is limited to the current user, so the store's temporary
    // files are protected too (HMS-SR-005). A folder that already existed is left as it is.
    {
        std::error_code ec;
        const auto parent = fs::path(canonical_).parent_path();
        if (!parent.empty() && !fs::exists(parent, ec) && fs::create_directories(parent, ec)) {
            std::string aclError;
            const auto u8 = parent.u8string();
            const std::string parentUtf8(u8.begin(), u8.end());
            if (!restrictToCurrentUser(parentUtf8, true, aclError)) warnings += aclError + "\n";
        }
    }

    // One instance per data store (SRS 2.4): take the lock before opening.
    std::string lockError;
    lock_ = StoreLock::acquire(canonical_, lockError);
    if (!lock_) {
        fatal("Cannot start: " + lockError + ".\nNothing was changed.");
        return;
    }
    {
        std::string aclError;
        if (storeFileExists(canonical_) && !restrictToCurrentUser(canonical_, false, aclError)) warnings += aclError + "\n";
        if (!restrictToCurrentUser(lock_->lockPath(), false, aclError)) warnings += aclError + "\n";
    }

    notice_.clear();
    if (!warnings.empty()) notice_.error("Warning: " + warnings);

    focusFirst_ = true;
    if (!storeFileExists(canonical_)) {
        createNew_ = true;
        phase_ = Phase::NewPassphrase;
    } else if (storeFileIsPlain(canonical_)) {
        phase_ = Phase::ConfirmEncryptPlain;
    } else {
        phase_ = Phase::EnterPassphrase;
    }
}

void App::submitNewPassphrase() {
    notice_.clear();
    std::string reason;
    if (!isStorePassphraseAcceptable(pass1_, reason)) {
        notice_.error(reason);
    } else if (pass1_ != pass2_) {
        notice_.error("The two passphrases do not match.");
    } else {
        runLater(encryptPlain_ ? "Encrypting the data store..." : "Creating the data store...",
                 [this]() { openWithNewPassphrase(); });
    }
}

void App::submitPassphrase() {
    runLater("Opening the data store...", [this]() { openWithPassphrase(); });
}

void App::submitLogin() {
    runLater("Signing in...", [this]() { signIn(); });
}

void App::openWithNewPassphrase() {
    store_ = openStore(canonical_, createNew_, pass1_, encryptPlain_);
    wipe(pass1_);
    wipe(pass2_);
    if (store_.status != StoreStatus::Opened) {
        fatal("Cannot start: " + store_.message);
        return;
    }
    notice_.clear();
    if (store_.encryptedNow) {
        notice_.info("The data store is now encrypted. Keep the passphrase safe: it cannot be recovered.");
    }
    storeOpened();
}

void App::openWithPassphrase() {
    // An empty key would mean "no encryption" to the store and be reported as a damaged file.
    if (pass1_.empty()) {
        notice_.error("Enter the passphrase.");
        focusFirst_ = true;
        return;
    }
    store_ = openStore(canonical_, false, pass1_);
    wipe(pass1_);
    if (store_.status == StoreStatus::WrongPassphrase) {
        ++passphraseAttempts_;
        if (passphraseAttempts_ >= 3) {
            fatal("The passphrase was wrong three times. Nothing was changed.\nStart the application again to retry.");
            return;
        }
        notice_.error("The passphrase is wrong, or the data store is damaged. Try again.");
        focusFirst_ = true;
        return;
    }
    if (store_.status != StoreStatus::Opened) {
        fatal("Cannot start: " + store_.message);
        return;
    }
    notice_.clear();
    storeOpened();
}

void App::storeOpened() {
    services_ = std::make_unique<Services>(*store_.db, *clock_, AppPolicy{});
    focusFirst_ = true;
    phase_ = auth::hasStaffAccounts(*store_.db) ? Phase::Login : Phase::FirstAdmin;
}

void App::createFirstAdmin() {
    notice_.clear();
    if (adminPass1_ != adminPass2_) {
        notice_.error("Passwords do not match.");
        return;
    }
    std::string reason;
    if (!auth::isPasswordAcceptable(adminPass1_, reason)) {
        notice_.error(reason);
        return;
    }
    if (!auth::createStaffAccount(*store_.db, adminUser_, adminPass1_, auth::Role::Administrator, *clock_)) {
        notice_.error("Could not create the account (empty name, spaces, or already used).");
        return;
    }
    wipe(adminPass1_);
    wipe(adminPass2_);
    notice_.info("Administrator account created. Please sign in.");
    loginUser_ = adminUser_;
    focusFirst_ = true;
    phase_ = Phase::Login;
}

void App::signIn() {
    const auto result = auth::login(*store_.db, loginUser_, loginPass_, *clock_, services_->policy.auth);
    wipe(loginPass_);
    focusFirst_ = true;
    if (result.status == auth::LoginStatus::AccountLocked) {
        notice_.error("This account is locked after repeated failed attempts. Try again in " +
                      std::to_string(Settings(*store_.db, services_->policy).lockoutMinutes()) + " minutes.");
        return;
    }
    if (result.status != auth::LoginStatus::Success) {
        notice_.error("Invalid username or password.");
        return;
    }

    Session session;
    session.userId = loginUser_;
    session.displayName = loginUser_;
    session.role = toSessionRole(result.role);
    session.simulated = false;
    ctx_ = std::make_unique<Context>(*services_, session, result.role);
    nav_ = navigationFor(result.role);
    active_ = 0;
    if (!nav_.empty()) nav_[0].screen->onOpen(*ctx_);
    notice_.clear();
    loginUser_.clear();
    phase_ = Phase::Main;
}

void App::signOut() {
    nav_.clear();
    ctx_.reset();
    active_ = 0;
    loginUser_.clear();
    wipe(loginPass_);
    notice_.info("Logged out.");
    focusFirst_ = true;
    phase_ = Phase::Login;
}

void App::runLater(std::string busyText, std::function<void()> work) {
    busy_ = std::move(busyText);
    pending_ = std::move(work);
    pendingWait_ = 1;  // one frame is drawn with the message before the work starts
}

// ---- Drawing -------------------------------------------------------------------------------

void App::frame() {
    if (pending_) {
        if (pendingWait_ > 0) {
            --pendingWait_;
        } else {
            auto work = std::move(pending_);
            pending_ = nullptr;
            work();
            busy_.clear();
        }
    }

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0F, 0.0F));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0F);
    const ImGuiWindowFlags flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                                   ImGuiWindowFlags_NoBringToFrontOnFocus;
    const bool open = ImGui::Begin("##root", nullptr, flags);
    ImGui::PopStyleVar(2);  // only the root window has no padding or border; its children keep theirs
    if (open) {
        if (phase_ == Phase::Main) {
            drawMain();
        } else {
            drawStartup();
        }
    }
    ImGui::End();
}

void App::drawCard(const char* title, const std::function<void()>& body) {
    const ImVec2 avail = ImGui::GetContentRegionAvail();
    const float width = std::min(avail.x - 32.0F, ImGui::GetFontSize() * 32.0F);
    ImGui::SetCursorPos(ImVec2((avail.x - width) * 0.5F, std::max(16.0F, avail.y * 0.10F)));
    if (ImGui::BeginChild("card", ImVec2(width, 0.0F),
                          ImGuiChildFlags_Borders | ImGuiChildFlags_AutoResizeY | ImGuiChildFlags_AlwaysUseWindowPadding)) {
        heading(title);
        body();
    }
    ImGui::EndChild();
}

void App::drawStartup() {
    const bool busy = !busy_.empty();
    auto focus = [this]() {
        if (focusFirst_) {
            ImGui::SetKeyboardFocusHere();
            focusFirst_ = false;
        }
    };
    auto busyLine = [&]() {
        if (busy) muted(busy_.c_str());
    };

    switch (phase_) {
        case Phase::ConfirmNewStore:
            drawCard("Hospital Management System", [&]() {
                ImGui::TextWrapped("No data store was found at:");
                mutedWrapped(canonical_);
                vspace();
                ImGui::TextWrapped("Set up a NEW, EMPTY data store here? If you expected existing data, quit and check the --store path.");
                vspace();
                if (primaryButton("Create a new data store")) prepareStore();
                ImGui::SameLine();
                if (plainButton("Quit")) quit_ = true;
            });
            break;

        case Phase::ConfirmEncryptPlain:
            drawCard("This data store is not encrypted", [&]() {
                mutedWrapped(canonical_);
                vspace();
                ImGui::TextWrapped(
                    "Patient data must not be kept in plain text. It will be encrypted in place with a passphrase you choose. "
                    "If you want a backup first, copy the file now; a copy stays readable by anyone until you delete it.");
                vspace();
                if (primaryButton("Encrypt the data store")) {
                    encryptPlain_ = true;
                    focusFirst_ = true;
                    phase_ = Phase::NewPassphrase;
                }
                ImGui::SameLine();
                if (plainButton("Quit")) quit_ = true;
            });
            break;

        case Phase::NewPassphrase:
            drawCard(encryptPlain_ ? "Choose a passphrase to encrypt the data" : "Choose a passphrase for the data store", [&]() {
                notice_.draw();
                ImGui::TextWrapped(
                    "The passphrase encrypts all patient data. It is asked at every start-up and cannot be recovered: "
                    "if it is lost, the data cannot be read. Use 8 to 128 characters.");
                vspace();
                ImGui::BeginDisabled(busy);
                focus();
                passwordField("Passphrase", pass1_);
                const bool submit = textField("Repeat the passphrase", pass2_, nullptr,
                                              ImGuiInputTextFlags_Password | ImGuiInputTextFlags_EnterReturnsTrue);
                vspace();
                const bool click = primaryButton(encryptPlain_ ? "Encrypt and open" : "Create the data store");
                ImGui::EndDisabled();
                busyLine();
                if ((submit || click) && !busy) submitNewPassphrase();
            });
            break;

        case Phase::EnterPassphrase:
            drawCard("Data store passphrase", [&]() {
                notice_.draw();
                mutedWrapped(canonical_);
                vspace();
                ImGui::BeginDisabled(busy);
                focus();
                const bool submit = textField("Passphrase", pass1_, nullptr,
                                              ImGuiInputTextFlags_Password | ImGuiInputTextFlags_EnterReturnsTrue);
                vspace();
                const bool click = primaryButton("Open the data store");
                ImGui::EndDisabled();
                busyLine();
                if ((submit || click) && !busy) submitPassphrase();
            });
            break;

        case Phase::FirstAdmin:
            drawCard("First-time setup: create the Administrator", [&]() {
                notice_.draw();
                ImGui::TextWrapped("Password: 8 or more characters with an upper-case letter, a lower-case letter, a digit and a symbol.");
                vspace();
                focus();
                textField("Administrator username", adminUser_);
                passwordField("Password", adminPass1_);
                const bool submit = textField("Repeat the password", adminPass2_, nullptr,
                                              ImGuiInputTextFlags_Password | ImGuiInputTextFlags_EnterReturnsTrue);
                vspace();
                if (primaryButton("Create the account") || submit) createFirstAdmin();
            });
            break;

        case Phase::Login:
            drawCard("Staff login", [&]() {
                notice_.draw();
                ImGui::BeginDisabled(busy);
                focus();
                textField("Username", loginUser_);
                const bool submit = textField("Password", loginPass_, nullptr,
                                              ImGuiInputTextFlags_Password | ImGuiInputTextFlags_EnterReturnsTrue);
                vspace();
                const bool click = primaryButton("Sign in");
                ImGui::EndDisabled();
                busyLine();
                if ((submit || click) && !busy) submitLogin();
            });
            break;

        case Phase::Fatal:
            drawCard("The application cannot start", [&]() {
                ImGui::TextWrapped("%s", fatal_.c_str());
                vspace();
                if (primaryButton("Close")) quit_ = true;
            });
            break;

        case Phase::Main: break;
    }
}

void App::goTo(const std::string& label) {
    for (std::size_t i = 0; i < nav_.size(); ++i) {
        if (nav_[i].label == label) {
            active_ = static_cast<int>(i);
            nav_[i].screen->onOpen(*ctx_);
            return;
        }
    }
}

void App::drawMain() {
    const float sidebarWidth = ImGui::GetFontSize() * 14.0F;

    ImGui::PushStyleColor(ImGuiCol_ChildBg, kSidebar);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.92F, 0.96F, 0.97F, 1.0F));
    ImGui::PushStyleColor(ImGuiCol_TextDisabled, ImVec4(0.62F, 0.74F, 0.78F, 1.0F));
    ImGui::PushStyleColor(ImGuiCol_Header, ImVec4(1.0F, 1.0F, 1.0F, 0.16F));
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, ImVec4(1.0F, 1.0F, 1.0F, 0.10F));
    ImGui::PushStyleColor(ImGuiCol_HeaderActive, ImVec4(1.0F, 1.0F, 1.0F, 0.24F));
    if (ImGui::BeginChild("sidebar", ImVec2(sidebarWidth, 0.0F), ImGuiChildFlags_AlwaysUseWindowPadding,
                          ImGuiWindowFlags_NoScrollbar)) {
        ImGui::SetWindowFontScale(1.15F);
        ImGui::TextUnformatted("Hospital");
        ImGui::TextUnformatted("Management");
        ImGui::SetWindowFontScale(1.0F);
        vspace();
        ImGui::TextUnformatted(ctx_->session.displayName.c_str());
        ImGui::TextDisabled("%s", roleName(ctx_->role));
        vspace();
        ImGui::Separator();
        vspace();
        for (std::size_t i = 0; i < nav_.size(); ++i) {
            ImGui::PushID(static_cast<int>(i));
            if (ImGui::Selectable(nav_[i].label.c_str(), active_ == static_cast<int>(i), 0, ImVec2(0.0F, ImGui::GetTextLineHeight() * 1.5F))) {
                if (active_ != static_cast<int>(i)) nav_[i].screen->onOpen(*ctx_);
                active_ = static_cast<int>(i);
            }
            ImGui::PopID();
        }
        const float logoutHeight = ImGui::GetFrameHeight();
        ImGui::SetCursorPosY(ImGui::GetWindowHeight() - logoutHeight - ImGui::GetStyle().WindowPadding.y);
        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(1.0F, 1.0F, 1.0F, 0.12F));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(1.0F, 1.0F, 1.0F, 0.22F));
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(1.0F, 1.0F, 1.0F, 0.30F));
        ImGui::PushStyleColor(ImGuiCol_Border, ImVec4(1.0F, 1.0F, 1.0F, 0.30F));
        const bool logOut = plainButton("Log out", -FLT_MIN);
        ImGui::PopStyleColor(4);
        if (logOut) signOut();
    }
    ImGui::EndChild();
    ImGui::PopStyleColor(6);
    if (phase_ != Phase::Main) return;  // logged out in the sidebar

    ImGui::SameLine(0.0F, 0.0F);
    if (ImGui::BeginChild("content", ImVec2(0.0F, 0.0F), ImGuiChildFlags_AlwaysUseWindowPadding)) {
        if (!nav_.empty()) nav_[static_cast<std::size_t>(active_)].screen->draw(*ctx_);
    }
    ImGui::EndChild();

    if (!ctx_->goTo.empty()) {
        const std::string target = std::move(ctx_->goTo);
        ctx_->goTo.clear();
        goTo(target);
    }
}

}  // namespace hms::gui
