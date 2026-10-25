#pragma once
// Per-store lock (SRS v11 draft section 2.4; SAD ADR-002): an exclusive
// operating-system lock on "<canonical store path>.lock", so a second
// instance of the application can never write to the same data store.
// LockFileEx on Windows, flock on Linux. The OS releases the lock when the
// process ends, so a crash leaves no stale lock.
//
// Current scope: canonical path via std::filesystem::weakly_canonical.
// Not yet done (logged as deviations): hard-link refusal, network-location
// refusal, Windows owner-only ACL on the lock file.

#include <memory>
#include <string>

namespace hms {

class StoreLock {
public:
    ~StoreLock();
    StoreLock(const StoreLock&) = delete;
    StoreLock& operator=(const StoreLock&) = delete;

    // Returns nullptr and fills `error` if the lock is held by another
    // process or the lock file cannot be created.
    static std::unique_ptr<StoreLock> acquire(const std::string& canonicalStorePath, std::string& error);

    const std::string& lockPath() const { return lockPath_; }

private:
    StoreLock() = default;
    std::string lockPath_;
#ifdef _WIN32
    void* handle_ = nullptr;
#else
    int fd_ = -1;
#endif
};

// Absolute, symlink-resolved form of a store path (UTF-8 in, UTF-8 out).
std::string canonicalStorePath(const std::string& utf8Path);

// Creates an empty file readable and writable by the owner only (POSIX mode
// 0600, created exclusively). On Windows the file is created with default
// permissions for now (listed in docs/deviation-log.md). Returns false if it already exists.
bool createOwnerOnlyFile(const std::string& utf8Path, std::string& error);

}  // namespace hms
