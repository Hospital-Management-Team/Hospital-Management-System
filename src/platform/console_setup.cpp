#include "platform/console_setup.hpp"

#ifdef _WIN32
#include <windows.h>
#endif

namespace hms {

void setUpConsoleUtf8() {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
#endif
}

}  // namespace hms
