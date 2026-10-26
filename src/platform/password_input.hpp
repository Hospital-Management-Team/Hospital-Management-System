#pragma once
// Reads a password without echoing it. If stdin is not a terminal
// (tests, pipes) it reads a normal line.
#include <iostream>
#include <string>
#ifdef _WIN32
#include <windows.h>
#else
#include <termios.h>
#include <unistd.h>
#endif

namespace hms {

inline bool readPassword(const std::string& prompt, std::string& out) {
    std::cout << prompt << std::flush;
#ifdef _WIN32
    HANDLE h = GetStdHandle(STD_INPUT_HANDLE);
    DWORD mode = 0;
    const bool tty = GetConsoleMode(h, &mode) != 0;
    if (tty) SetConsoleMode(h, mode & ~static_cast<DWORD>(ENABLE_ECHO_INPUT));
#else
    termios old{};
    const bool tty = isatty(STDIN_FILENO) && tcgetattr(STDIN_FILENO, &old) == 0;
    if (tty) {
        termios t = old;
        t.c_lflag &= ~static_cast<tcflag_t>(ECHO);
        tcsetattr(STDIN_FILENO, TCSANOW, &t);
    }
#endif
    const bool ok = static_cast<bool>(std::getline(std::cin, out));
#ifdef _WIN32
    if (tty) SetConsoleMode(h, mode);
#else
    if (tty) tcsetattr(STDIN_FILENO, TCSANOW, &old);
#endif
    if (tty) std::cout << "\n";
    if (!out.empty() && out.back() == '\r') out.pop_back();
    return ok;
}

}  // namespace hms