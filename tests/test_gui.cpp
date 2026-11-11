// Tests of the graphical front end. Dear ImGui runs without a window here: the application and its
// screens are drawn frame by frame into an off-screen context, and the steps the buttons trigger
// are called directly (the services and rules behind them are tested in the other files).
// ImGui checks its own use (pushes and pops, ids), so a screen that misuses it fails these tests.

#include <filesystem>
#include <string>
#include <vector>

#include "doctest.h"
#include "gui/gui_app.hpp"
#include "imgui.h"
#include "imgui_internal.h"
#include "test_support.hpp"

namespace hms::gui {

// The tests reach the private steps that the buttons call.
struct AppTestAccess {
    static std::string& pass1(App& a) { return a.pass1_; }
    static std::string& pass2(App& a) { return a.pass2_; }
    static std::string& adminUser(App& a) { return a.adminUser_; }
    static std::string& adminPass1(App& a) { return a.adminPass1_; }
    static std::string& adminPass2(App& a) { return a.adminPass2_; }
    static std::string& loginUser(App& a) { return a.loginUser_; }
    static std::string& loginPass(App& a) { return a.loginPass_; }
    static void createStore(App& a) { a.prepareStore(); }
    static void submitNewPassphrase(App& a) { a.submitNewPassphrase(); }
    static void submitPassphrase(App& a) { a.submitPassphrase(); }
    static void submitLogin(App& a) { a.submitLogin(); }
    static void createFirstAdmin(App& a) { a.createFirstAdmin(); }
    static void signOut(App& a) { a.signOut(); }
    static std::size_t screenCount(App& a) { return a.nav_.size(); }
    static void open(App& a, std::size_t i) {
        a.active_ = static_cast<int>(i);
        a.nav_[i].screen->onOpen(*a.ctx_);
    }
    static Context& context(App& a) { return *a.ctx_; }
};

}  // namespace hms::gui

namespace {

using namespace hms;
using namespace hms::gui;

// An off-screen Dear ImGui context.
struct Headless {
    Headless() {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        io.DisplaySize = ImVec2(1280.0F, 800.0F);
        unsigned char* pixels = nullptr;
        int width = 0;
        int height = 0;
        io.Fonts->GetTexDataAsRGBA32(&pixels, &width, &height);
        applyTheme(1.0F);
    }
    ~Headless() { ImGui::DestroyContext(); }
    Headless(const Headless&) = delete;
    Headless& operator=(const Headless&) = delete;

    static void frames(App& app, int count = 4) {
        for (int i = 0; i < count; ++i) {
            ImGui::GetIO().DeltaTime = 1.0F / 60.0F;
            ImGui::NewFrame();
            app.frame();
            ImGui::Render();
        }
    }
};

// Puts text into a form field. A field that has the keyboard focus keeps its own copy of the text
// and would overwrite the new value, so the focus is dropped first.
void typeInto(std::string& field, const std::string& text) {
    ImGui::ClearActiveID();
    field = text;
}

struct TempFolder {
    std::filesystem::path dir = test::uniqueTempDir();
    ~TempFolder() {
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }
};

Session sessionOf(const char* user, Role role) {
    Session s;
    s.userId = user;
    s.displayName = user;
    s.role = role;
    return s;
}

// A store that already has one account of every role, a doctor with weekly hours, a patient, an
// appointment, a medicine and a department: enough for every screen to show something.
struct SeededStore : TempFolder {
    std::string path = (dir / "hms.db").string();
    FixedClock clock{test::fixedNow()};

    SeededStore() {
        std::string error;
        auto lock = StoreLock::acquire(canonicalStorePath(path), error);
        auto opened = openStore(path, true, test::kTestPassphrase);
        REQUIRE(opened.status == StoreStatus::Opened);
        Database& db = *opened.db;

        REQUIRE(auth::createStaffAccount(db, "admin", "Admin@123", auth::Role::Administrator, clock));
        REQUIRE(auth::createStaffAccount(db, "doc", "Doctor@123", auth::Role::Doctor, clock));
        REQUIRE(auth::createStaffAccount(db, "rec", "Recept@123", auth::Role::Receptionist, clock));
        REQUIRE(auth::createStaffAccount(db, "lab", "Lab@12345", auth::Role::LabTechnician, clock));
        REQUIRE(auth::createStaffAccount(db, "ph", "Pharm@123", auth::Role::Pharmacist, clock));

        Services svc(db, clock, AppPolicy{});
        const Session admin = sessionOf("admin", Role::Admin);
        const Session reception = sessionOf("rec", Role::Receptionist);
        const Session pharmacist = sessionOf("ph", Role::Pharmacist);

        auto doctors = svc.appointments.listDoctors(admin);
        REQUIRE(doctors);
        std::int64_t doctorId = 0;
        for (const auto& d : doctors.value()) {
            if (d.username == "doc") doctorId = d.staffId;
        }
        REQUIRE(doctorId != 0);
        DoctorScheduleDraft schedule;
        schedule.department = "General Medicine";
        for (int day = 1; day <= 7; ++day) schedule.hours.push_back(WeeklyHours{day, 9 * 60, 18 * 60});
        REQUIRE(svc.appointments.setDoctorSchedule(admin, doctorId, schedule));

        PatientDraft patient;
        patient.fullName = "Meera Shah";
        patient.dateOfBirth = "03-03-1990";
        patient.gender = "F";
        patient.address = "5 Park Lane, Bengaluru";
        patient.phone = "9123456780";
        auto registered = svc.patients.registerPatient(reception, patient, DuplicateDecision::none());
        REQUIRE(registered);
        REQUIRE(svc.appointments.book(reception, registered.value().uhid, doctorId, LocalSlot{Date{2026, 10, 26}, 10 * 60}));
        REQUIRE(svc.pharmacy.addMedicine(pharmacist, MedicineDraft{"Paracetamol 500mg", "tablet", "100"}));
    }
};

std::filesystem::path newStorePath(const TempFolder& f) { return f.dir / "data" / "hms.db"; }

// Gets an App to the login screen of an existing seeded store.
void openSeeded(App& app) {
    REQUIRE(app.phase() == App::Phase::EnterPassphrase);
    typeInto(AppTestAccess::pass1(app), test::kTestPassphrase);
    AppTestAccess::submitPassphrase(app);
    Headless::frames(app);
    REQUIRE(app.phase() == App::Phase::Login);
}

void signIn(App& app, const std::string& user, const std::string& password) {
    typeInto(AppTestAccess::loginUser(app), user);
    typeInto(AppTestAccess::loginPass(app), password);
    AppTestAccess::submitLogin(app);
    Headless::frames(app);
}

}  // namespace

TEST_CASE("GUI start-up: nothing is created until the user agrees; a new store is encrypted with the chosen passphrase") {
    Headless gui;
    TempFolder folder;
    const auto path = newStorePath(folder);
    FixedClock clock{test::fixedNow()};
    {
        App app(StartupOptions{path.string()}, &clock);
        REQUIRE(app.phase() == App::Phase::ConfirmNewStore);
        Headless::frames(app);
        CHECK_FALSE(std::filesystem::exists(path.parent_path()));  // not even the folder

        AppTestAccess::createStore(app);
        REQUIRE(app.phase() == App::Phase::NewPassphrase);
        CHECK(std::filesystem::exists(path.parent_path()));
        CHECK_FALSE(storeFileExists(path.string()));

        // too short, then two different ones: refused, no store is written
        typeInto(AppTestAccess::pass1(app), "short");
        typeInto(AppTestAccess::pass2(app), "short");
        AppTestAccess::submitNewPassphrase(app);
        Headless::frames(app);
        CHECK(app.phase() == App::Phase::NewPassphrase);
        typeInto(AppTestAccess::pass1(app), "a long enough one");
        typeInto(AppTestAccess::pass2(app), "a different one!");
        AppTestAccess::submitNewPassphrase(app);
        Headless::frames(app);
        CHECK(app.phase() == App::Phase::NewPassphrase);
        CHECK_FALSE(storeFileExists(path.string()));

        typeInto(AppTestAccess::pass1(app), "a long enough one");
        typeInto(AppTestAccess::pass2(app), "a long enough one");
        AppTestAccess::submitNewPassphrase(app);
        Headless::frames(app);
        REQUIRE(app.phase() == App::Phase::FirstAdmin);
        CHECK(storeFileExists(path.string()));
        CHECK_FALSE(storeFileIsPlain(path.string()));
        CHECK(AppTestAccess::pass1(app).empty());  // the passphrase is wiped once the store is open

        // first Administrator: a weak password is refused
        typeInto(AppTestAccess::adminUser(app), "admin");
        typeInto(AppTestAccess::adminPass1(app), "weak");
        typeInto(AppTestAccess::adminPass2(app), "weak");
        AppTestAccess::createFirstAdmin(app);
        CHECK(app.phase() == App::Phase::FirstAdmin);
        typeInto(AppTestAccess::adminPass1(app), "Admin@123");
        typeInto(AppTestAccess::adminPass2(app), "Admin@123");
        AppTestAccess::createFirstAdmin(app);
        REQUIRE(app.phase() == App::Phase::Login);

        signIn(app, "admin", "wrong password");
        CHECK(app.phase() == App::Phase::Login);
        signIn(app, "admin", "Admin@123");
        REQUIRE(app.phase() == App::Phase::Main);
        CHECK(app.activeScreen() == "Staff accounts");
        CHECK(AppTestAccess::loginPass(app).empty());

        AppTestAccess::signOut(app);
        CHECK(app.phase() == App::Phase::Login);
        Headless::frames(app);
    }
}

TEST_CASE("GUI start-up: an existing store asks for its passphrase; three wrong ones end the start-up") {
    Headless gui;
    SeededStore seeded;
    {
        App app(StartupOptions{seeded.path}, &seeded.clock);
        REQUIRE(app.phase() == App::Phase::EnterPassphrase);
        // nothing typed: asked again, not counted as a wrong passphrase, and the store is not reported damaged
        for (int i = 0; i < 4; ++i) {
            typeInto(AppTestAccess::pass1(app), "");
            AppTestAccess::submitPassphrase(app);
            Headless::frames(app);
            CHECK(app.phase() == App::Phase::EnterPassphrase);
        }
        for (int attempt = 0; attempt < 3; ++attempt) {
            CAPTURE(attempt);
            CAPTURE(app.fatalMessage());
            CHECK(app.phase() == App::Phase::EnterPassphrase);
            typeInto(AppTestAccess::pass1(app), "not the passphrase");
            AppTestAccess::submitPassphrase(app);
            Headless::frames(app);
        }
        CHECK(app.phase() == App::Phase::Fatal);
        Headless::frames(app);  // the message screen draws
    }
    {
        App app(StartupOptions{seeded.path}, &seeded.clock);
        openSeeded(app);
    }
}

TEST_CASE("GUI start-up: a second instance on the same store is refused") {
    Headless gui;
    SeededStore seeded;
    App first(StartupOptions{seeded.path}, &seeded.clock);
    REQUIRE(first.phase() == App::Phase::EnterPassphrase);
    App second(StartupOptions{seeded.path}, &seeded.clock);
    CHECK(second.phase() == App::Phase::Fatal);
    CHECK(second.fatalMessage().find("already in use") != std::string::npos);
    Headless::frames(second);
}

TEST_CASE("GUI: every screen of every role draws, with and without data") {
    Headless gui;
    SeededStore seeded;
    struct Login {
        const char* user;
        const char* password;
        std::size_t screens;
    };
    const Login logins[] = {{"rec", "Recept@123", 9}, {"doc", "Doctor@123", 7}, {"lab", "Lab@12345", 2},
                            {"ph", "Pharm@123", 3},   {"admin", "Admin@123", 7}};
    for (const auto& login : logins) {
        CAPTURE(login.user);
        App app(StartupOptions{seeded.path}, &seeded.clock);
        openSeeded(app);
        signIn(app, login.user, login.password);
        REQUIRE(app.phase() == App::Phase::Main);
        REQUIRE(AppTestAccess::screenCount(app) == login.screens);
        for (std::size_t i = 0; i < login.screens; ++i) {
            AppTestAccess::open(app, i);
            Headless::frames(app, 3);
            CHECK(app.phase() == App::Phase::Main);
        }
        // a patient found on one screen is offered on the others
        AppTestAccess::context(app).currentUhid = 1;
        for (std::size_t i = 0; i < login.screens; ++i) {
            AppTestAccess::open(app, i);
            Headless::frames(app, 3);
        }
        AppTestAccess::signOut(app);
        Headless::frames(app);
        CHECK(app.phase() == App::Phase::Login);
    }
}

TEST_CASE("GUI: Lab Technician and Pharmacist select a patient by UHID without opening the patient record") {
    // These roles may not view patient records (SRS 2.3), so the UHID box on their screens only
    // checks that the UHID is well formed, as the console menus do. Opening the record would be
    // refused and audited as a denied PATIENT_VIEW.
    Headless gui;
    SeededStore seeded;
    struct Case {
        const char* user;
        const char* password;
        const char* screen;
    };
    const Case cases[] = {{"lab", "Lab@12345", "Tests of a patient"}, {"ph", "Pharm@123", "Prescriptions of a patient"}};
    for (const auto& c : cases) {
        CAPTURE(c.user);
        App app(StartupOptions{seeded.path}, &seeded.clock);
        openSeeded(app);
        signIn(app, c.user, c.password);
        REQUIRE(app.phase() == App::Phase::Main);
        AppTestAccess::context(app).goTo = c.screen;
        Headless::frames(app);
        REQUIRE(app.activeScreen() == c.screen);
        AppTestAccess::context(app).currentUhid = 1;  // the screen follows the selected patient
        Headless::frames(app, 3);
        CHECK(app.phase() == App::Phase::Main);
    }
    Database db(seeded.path, Database::Mode::ReadWrite, test::kTestPassphrase);
    auto denied = db.queryInt("SELECT count(*) FROM audit_entry WHERE action = 'PATIENT_VIEW' AND outcome = 'DENIED'");
    REQUIRE(denied);
    CHECK(*denied == 0);
}

TEST_CASE("GUI: a screen can ask for another one, and only the role's own screens exist") {
    Headless gui;
    SeededStore seeded;
    App app(StartupOptions{seeded.path}, &seeded.clock);
    openSeeded(app);
    signIn(app, "rec", "Recept@123");
    REQUIRE(app.phase() == App::Phase::Main);
    CHECK(app.activeScreen() == "Register patient");

    AppTestAccess::context(app).goTo = "Bills and balances";
    Headless::frames(app);
    CHECK(app.activeScreen() == "Bills and balances");

    AppTestAccess::context(app).goTo = "Medical history";  // a Doctor screen: not in this menu
    Headless::frames(app);
    CHECK(app.activeScreen() == "Bills and balances");
}

TEST_CASE("GUI: the menus are the same screens as the console's roles") {
    const std::vector<std::string> doctor = {"My appointments",   "Record consultation", "Patients",       "Past consultations",
                                             "Medical history",   "Lab results",         "My weekly hours"};
    auto nav = navigationFor(auth::Role::Doctor);
    REQUIRE(nav.size() == doctor.size());
    for (std::size_t i = 0; i < doctor.size(); ++i) CHECK(nav[i].label == doctor[i]);
    CHECK(navigationFor(auth::Role::Administrator).size() == 7);
    CHECK(navigationFor(auth::Role::Pharmacist).size() == 3);
}
