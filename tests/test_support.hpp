#pragma once
// Shared test fixtures: unique temporary folders, a fixed clock instant
// (25-10-2026, 11:30 IST) and an opened, locked data store.

#include <atomic>
#include <chrono>
#include <filesystem>
#include <memory>
#include <string>

#include "core/clock.hpp"
#include "data/store.hpp"
#include "platform/store_lock.hpp"

namespace hms::test {

inline std::filesystem::path uniqueTempDir() {
    static std::atomic<int> counter{0};
    auto base = std::filesystem::temp_directory_path() /
                ("hms_test_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "_" +
                 std::to_string(counter++));
    std::filesystem::create_directories(base);
    return base;
}

inline UtcSeconds fixedNow() { return makeUtc(2026, 10, 25, 6, 0, 0); }  // 11:30 in Asia/Kolkata

// A new data store in its own temporary folder, locked and opened.
struct TempStore {
    std::filesystem::path dir = uniqueTempDir();
    std::string path = (dir / "hms_test.db").string();
    std::unique_ptr<StoreLock> lock;
    StoreOpenResult store;
    FixedClock clock{fixedNow()};

    TempStore() {
        std::string err;
        lock = StoreLock::acquire(canonicalStorePath(path), err);
        store = openStore(path, true);
    }
    virtual ~TempStore() {
        store.db.reset();
        lock.reset();
        std::error_code ec;
        std::filesystem::remove_all(dir, ec);
    }
    Database& db() { return *store.db; }
    std::int64_t count(const std::string& sql) { return db().queryInt(sql).value_or(-1); }
};

}  // namespace hms::test
