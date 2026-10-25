#include "core/ids.hpp"

#include <sqlite3.h>

namespace hms {

std::string newEventId() {
    unsigned char bytes[16];
    // SQLite's PRNG is seeded from the operating system's entropy source.
    sqlite3_randomness(static_cast<int>(sizeof bytes), bytes);
    static const char* kHex = "0123456789abcdef";
    std::string out;
    out.reserve(32);
    for (unsigned char b : bytes) {
        out.push_back(kHex[b >> 4]);
        out.push_back(kHex[b & 0x0F]);
    }
    return out;
}

}  // namespace hms
