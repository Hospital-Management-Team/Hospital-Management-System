#include "platform/store_lock.hpp"

#include <filesystem>
#include <string>
#include <system_error>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <aclapi.h>
#include <sddl.h>
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

namespace {
// A security descriptor that gives the current Windows user full control and nobody else any
// access: a protected DACL with one entry (HMS-SR-005; deviation D-07). For a folder the entry is
// inherited by what is created inside it, so SQLite's temporary files get the same protection.
class OwnerOnlySecurity {
public:
    explicit OwnerOnlySecurity(bool folder) {
        HANDLE token = nullptr;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
            lastError_ = GetLastError();
            return;
        }
        DWORD size = 0;
        GetTokenInformation(token, TokenUser, nullptr, 0, &size);
        std::vector<unsigned char> buf(size ? size : 1);
        if (size == 0 || !GetTokenInformation(token, TokenUser, buf.data(), size, &size)) {
            lastError_ = GetLastError();
            CloseHandle(token);
            return;
        }
        CloseHandle(token);
        LPWSTR sidText = nullptr;
        if (!ConvertSidToStringSidW(reinterpret_cast<TOKEN_USER*>(buf.data())->User.Sid, &sidText)) {
            lastError_ = GetLastError();
            return;
        }
        const std::wstring sddl = std::wstring(L"D:P(A;") + (folder ? L"OICI" : L"") + L";FA;;;" + sidText + L")";
        LocalFree(sidText);
        if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(sddl.c_str(), SDDL_REVISION_1, &descriptor_,
                                                                  nullptr)) {
            lastError_ = GetLastError();
            descriptor_ = nullptr;
        }
    }
    ~OwnerOnlySecurity() {
        if (descriptor_) LocalFree(descriptor_);
    }
    OwnerOnlySecurity(const OwnerOnlySecurity&) = delete;
    OwnerOnlySecurity& operator=(const OwnerOnlySecurity&) = delete;

    bool ok() const { return descriptor_ != nullptr; }
    DWORD lastError() const { return lastError_; }
    SECURITY_ATTRIBUTES attributes() const {
        SECURITY_ATTRIBUTES sa{};
        sa.nLength = sizeof(sa);
        sa.lpSecurityDescriptor = descriptor_;
        sa.bInheritHandle = FALSE;
        return sa;
    }
    PACL dacl() const {
        BOOL present = FALSE, defaulted = FALSE;
        PACL acl = nullptr;
        if (!GetSecurityDescriptorDacl(descriptor_, &present, &acl, &defaulted) || !present) return nullptr;
        return acl;
    }

private:
    PSECURITY_DESCRIPTOR descriptor_ = nullptr;
    DWORD lastError_ = 0;
};
}  // namespace

bool restrictToCurrentUser(const std::string& utf8Path, bool isFolder, std::string& error) {
    OwnerOnlySecurity sec(isFolder);
    if (!sec.ok()) {
        error = "cannot build the owner-only permissions (Windows error " + std::to_string(sec.lastError()) + ")";
        return false;
    }
    const std::wstring wpath = fromUtf8(utf8Path).wstring();
    std::vector<wchar_t> mutablePath(wpath.begin(), wpath.end());
    mutablePath.push_back(L'\0');
    const DWORD rc = SetNamedSecurityInfoW(mutablePath.data(), SE_FILE_OBJECT,
                                           DACL_SECURITY_INFORMATION | PROTECTED_DACL_SECURITY_INFORMATION, nullptr,
                                           nullptr, sec.dacl(), nullptr);
    if (rc != ERROR_SUCCESS) {
        error = "cannot restrict " + utf8Path + " to the current user (Windows error " + std::to_string(rc) + ")";
        return false;
    }
    return true;
}

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
    // A new lock file is created for the current user only; an existing one is tightened below.
    OwnerOnlySecurity sec(false);
    SECURITY_ATTRIBUTES sa = sec.attributes();
    HANDLE h = CreateFileW(wpath.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                           sec.ok() ? &sa : nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
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
    OwnerOnlySecurity sec(false);
    if (!sec.ok()) {
        error = "cannot build the owner-only permissions (Windows error " + std::to_string(sec.lastError()) + ")";
        return false;
    }
    SECURITY_ATTRIBUTES sa = sec.attributes();
    HANDLE h = CreateFileW(wpath.c_str(), GENERIC_READ | GENERIC_WRITE, 0, &sa, CREATE_NEW, FILE_ATTRIBUTE_NORMAL,
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

bool restrictToCurrentUser(const std::string& utf8Path, bool isFolder, std::string& error) {
    if (chmod(utf8Path.c_str(), isFolder ? (S_IRUSR | S_IWUSR | S_IXUSR) : (S_IRUSR | S_IWUSR)) != 0) {
        error = "cannot restrict " + utf8Path + " to the current user: " + std::strerror(errno);
        return false;
    }
    return true;
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
