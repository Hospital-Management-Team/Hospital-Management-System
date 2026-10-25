#include "platform/store_lock.hpp"

#include <filesystem>
#include <system_error>

#ifdef _WIN32
#include <windows.h>
#else
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace fs = std::filesystem;

namespace hms {

namespace {
fs::path fromUtf8(const std::string& s) {
#if defined(__cpp_char8_t)
    return fs::path(reinterpret_cast<const char8_t*>(s.c_str()));
#else
    return fs::u8path(s);
#endif
}

std::string toUtf8(const fs::path& p) {
    auto u8 = p.u8string();
    return std::string(u8.begin(), u8.end());
}
}  // namespace

std::string canonicalStorePath(const std::string& utf8Path) {
    std::error_code ec;
    fs::path abs = fs::absolute(fromUtf8(utf8Path), ec);
    if (ec) return utf8Path;
    fs::path canon = fs::weakly_canonical(abs, ec);
    if (ec) return toUtf8(abs);
    return toUtf8(canon);
}

#ifdef _WIN32

StoreLock::~StoreLock() {
    if (handle_) {
        OVERLAPPED ov{};
        UnlockFileEx(static_cast<HANDLE>(handle_), 0, 1, 0, &ov);
        CloseHandle(static_cast<HANDLE>(handle_));
    }
}

std::unique_ptr<StoreLock> StoreLock::acquire(const std::string& canonicalStorePath, std::string& error) {
    std::unique_ptr<StoreLock> lock(new StoreLock());
    lock->lockPath_ = canonicalStorePath + ".lock";
    const std::wstring wpath = fromUtf8(lock->lockPath_).wstring();
    HANDLE h = CreateFileW(wpath.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        error = "cannot open lock file " + lock->lockPath_ + " (Windows error " + std::to_string(GetLastError()) + ")";
        return nullptr;
    }
    OVERLAPPED ov{};
    if (!LockFileEx(h, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, 1, 0, &ov)) {
        CloseHandle(h);
        error = "the data store is already in use by another instance of the application";
        return nullptr;
    }
    lock->handle_ = h;
    return lock;
}

bool createOwnerOnlyFile(const std::string& utf8Path, std::string& error) {
    const std::wstring wpath = fromUtf8(utf8Path).wstring();
    HANDLE h = CreateFileW(wpath.c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL,
                           nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        error = "cannot create " + utf8Path + " (Windows error " + std::to_string(GetLastError()) + ")";
        return false;
    }
    CloseHandle(h);
    return true;
}

#else

StoreLock::~StoreLock() {
    if (fd_ >= 0) {
        flock(fd_, LOCK_UN);
        close(fd_);
    }
}

std::unique_ptr<StoreLock> StoreLock::acquire(const std::string& canonicalStorePath, std::string& error) {
    std::unique_ptr<StoreLock> lock(new StoreLock());
    lock->lockPath_ = canonicalStorePath + ".lock";
    const int fd = open(lock->lockPath_.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
    if (fd < 0) {
        error = "cannot open lock file " + lock->lockPath_ + ": " + std::strerror(errno);
        return nullptr;
    }
    if (flock(fd, LOCK_EX | LOCK_NB) != 0) {
        close(fd);
        error = "the data store is already in use by another instance of the application";
        return nullptr;
    }
    lock->fd_ = fd;
    return lock;
}

bool createOwnerOnlyFile(const std::string& utf8Path, std::string& error) {
    const int fd = open(utf8Path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    if (fd < 0) {
        error = "cannot create " + utf8Path + ": " + std::strerror(errno);
        return false;
    }
    // Enforce 0600 regardless of the umask (SRS HMS-SR-005(a)).
    fchmod(fd, S_IRUSR | S_IWUSR);
    close(fd);
    return true;
}

#endif

}  // namespace hms
