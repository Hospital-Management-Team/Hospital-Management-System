#pragma once
// The application window of the graphical front end: start-up (data store, passphrase, first
// Administrator), staff login, and the main window with the menu of the logged-in role.
//
// This class draws with Dear ImGui only. It owns no window or OpenGL code, so the tests can run
// it frame by frame without a display (see main_gui.cpp for the GLFW/OpenGL loop).

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "core/clock.hpp"
#include "data/store.hpp"
#include "gui/context.hpp"
#include "gui/screens.hpp"
#include "platform/store_lock.hpp"

namespace hms::gui {

struct StartupOptions {
    std::string storePath = "database/hospital.db";
};

class App {
public:
    enum class Phase {
        ConfirmNewStore,      // no data store at the path: ask before creating one
        ConfirmEncryptPlain,  // an older, unencrypted store: ask before encrypting it
        NewPassphrase,        // choose the passphrase of a new or newly encrypted store
        EnterPassphrase,      // open an existing encrypted store
        FirstAdmin,           // an empty store: create the first Administrator
        Login,
        Main,
        Fatal                 // the application cannot go on; shows why
    };

    // `clock` is used by the services; the system clock when null (tests pass a fixed one).
    explicit App(StartupOptions options, const Clock* clock = nullptr);
    ~App();
    App(const App&) = delete;
    App& operator=(const App&) = delete;

    // Draws one frame. Call between ImGui::NewFrame() and ImGui::Render().
    void frame();

    bool wantsQuit() const { return quit_; }
    Phase phase() const { return phase_; }
    const std::string& fatalMessage() const { return fatal_; }
    // The label of the screen shown in the main window (empty before login).
    std::string activeScreen() const;

private:
    friend struct AppTestAccess;  // the tests drive the same steps the buttons do

    // start-up steps
    void begin();
    void prepareStore();
    void submitNewPassphrase();
    void submitPassphrase();
    void submitLogin();
    void openWithNewPassphrase();
    void openWithPassphrase();
    void storeOpened();
    void createFirstAdmin();
    void signIn();
    void signOut();
    void fatal(std::string message);
    void runLater(std::string busyText, std::function<void()> work);

    // drawing
    void drawCard(const char* title, const std::function<void()>& body);
    void drawStartup();
    void drawMain();
    void goTo(const std::string& label);

    StartupOptions options_;
    SystemClock systemClock_;
    const Clock* clock_;
    Phase phase_ = Phase::Fatal;
    std::string fatal_;
    bool quit_ = false;

    std::string canonical_;
    bool createNew_ = false;
    bool encryptPlain_ = false;
    int passphraseAttempts_ = 0;

    // Declared in this order so that they are released in the reverse order: screens and
    // services first, then the open store, and the store lock last.
    std::unique_ptr<StoreLock> lock_;
    StoreOpenResult store_;
    std::unique_ptr<Services> services_;
    std::unique_ptr<Context> ctx_;
    std::vector<NavEntry> nav_;
    int active_ = 0;

    // form fields
    std::string pass1_, pass2_;
    std::string adminUser_, adminPass1_, adminPass2_;
    std::string loginUser_, loginPass_;
    Notice notice_;
    bool focusFirst_ = true;

    std::function<void()> pending_;
    int pendingWait_ = 0;
    std::string busy_;
};

}  // namespace hms::gui
