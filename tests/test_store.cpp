// Store foundation (issue #1): creation, durability settings, persistence
// across restarts, schema-version refusal, damaged-store refusal, per-store
// lock and all-or-nothing transactions.
#include <doctest.h>

#include <fstream>
#include <iterator>

#include "data/schema.hpp"
#include "test_support.hpp"

#ifdef _WIN32
#include <windows.h>
#include <aclapi.h>
#include <sddl.h>
#else
#include <sys/stat.h>
#endif

using namespace hms;
namespace fs = std::filesystem;

namespace {
#ifdef _WIN32
// True when the file's ACL is protected (inherits nothing) and holds exactly one entry: the
// current user, with access allowed (HMS-SR-005, TC-Sec-06).
bool onlyCurrentUserHasAccess(const fs::path& p) {
    PACL dacl = nullptr;
    PSECURITY_DESCRIPTOR sd = nullptr;
    if (GetNamedSecurityInfoW(p.wstring().c_str(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, &dacl,
                              nullptr, &sd) != ERROR_SUCCESS) {
        return false;
    }
    bool ok = false;
    SECURITY_DESCRIPTOR_CONTROL control = 0;
    DWORD revision = 0;
    if (dacl && GetSecurityDescriptorControl(sd, &control, &revision) && (control & SE_DACL_PROTECTED) &&
        dacl->AceCount == 1) {
        void* ace = nullptr;
        if (GetAce(dacl, 0, &ace) && static_cast<ACE_HEADER*>(ace)->AceType == ACCESS_ALLOWED_ACE_TYPE) {
            HANDLE token = nullptr;
            if (OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &token)) {
                DWORD size = 0;
                GetTokenInformation(token, TokenUser, nullptr, 0, &size);
                std::string buf(size, '\0');
                if (GetTokenInformation(token, TokenUser, buf.data(), size, &size)) {
                    PSID user = reinterpret_cast<TOKEN_USER*>(buf.data())->User.Sid;
                    PSID aceSid = &static_cast<ACCESS_ALLOWED_ACE*>(ace)->SidStart;
                    ok = EqualSid(user, aceSid) != 0;
                }
                CloseHandle(token);
            }
        }
    }
    LocalFree(sd);
    return ok;
}
#endif

std::string readFile(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}
}  // namespace

TEST_CASE("a missing store is not created unless creation is allowed") {
    auto dir = test::uniqueTempDir();
    const auto path = (dir / "none.db").string();
    auto r = openStore(path, false);
    CHECK(r.status == StoreStatus::NotFound);
    CHECK_FALSE(fs::exists(path));
    fs::remove_all(dir);
}

TEST_CASE("a new store is created owner-only with the durability settings") {
    test::TempStore s;
    REQUIRE(s.store.status == StoreStatus::Opened);
    CHECK(s.store.created);
    CHECK(s.count("PRAGMA user_version") == schema::currentVersion());
    CHECK(s.count("PRAGMA foreign_keys") == 1);
    CHECK(s.count("PRAGMA synchronous") == 2);  // FULL
#ifndef _WIN32
    struct stat st {};
    REQUIRE(stat(s.path.c_str(), &st) == 0);
    CHECK((st.st_mode & 0777) == 0600);  // owner-only (HMS-SR-005)
#endif
}

// ------------------------------------------------------------ encryption (HMS-NF-003, TC-Sec-01)

TEST_CASE("TC-Sec-01 an encrypted store has no plain header and no readable patient text") {
    auto dir = test::uniqueTempDir();
    const auto path = (dir / "enc.db").string();
    const std::string marker = "ZebulonQuuxMarker 9876543210";
    {
        auto s = openStore(path, true, test::kTestPassphrase);
        REQUIRE(s.status == StoreStatus::Opened);
        Transaction tx(*s.db);
        s.db->exec("CREATE TABLE probe (v TEXT); INSERT INTO probe VALUES ('" + marker + "');");
        tx.commit();
    }
    const std::string bytes = readFile(path);
    REQUIRE(bytes.size() > 16);
    CHECK_FALSE(storeFileIsPlain(path));
    CHECK(bytes.compare(0, 15, "SQLite format 3") != 0);
    CHECK(bytes.find(marker) == std::string::npos);
    CHECK(bytes.find("ZebulonQuux") == std::string::npos);
    CHECK(bytes.find("CREATE TABLE") == std::string::npos);  // not even the table definitions

    // The same data in a plain store is readable, which is what this test guards against.
    const auto plainPath = (dir / "plain.db").string();
    {
        auto s = openStore(plainPath, true);
        REQUIRE(s.status == StoreStatus::Opened);
        Transaction tx(*s.db);
        s.db->exec("CREATE TABLE probe (v TEXT); INSERT INTO probe VALUES ('" + marker + "');");
        tx.commit();
    }
    CHECK(storeFileIsPlain(plainPath));
    CHECK(readFile(plainPath).find(marker) != std::string::npos);
    fs::remove_all(dir);
}

TEST_CASE("TC-Sec-01 the right passphrase opens the store again; a wrong one is refused and changes nothing") {
    auto dir = test::uniqueTempDir();
    const auto path = (dir / "enc.db").string();
    {
        auto s = openStore(path, true, test::kTestPassphrase);
        REQUIRE(s.status == StoreStatus::Opened);
        Transaction tx(*s.db);
        s.db->exec("CREATE TABLE probe (v TEXT); INSERT INTO probe VALUES ('kept');");
        tx.commit();
    }
    const std::string before = readFile(path);

    auto wrong = openStore(path, false, "not-the-passphrase");
    CHECK(wrong.status == StoreStatus::WrongPassphrase);
    CHECK(wrong.db == nullptr);
    CHECK(readFile(path) == before);

    auto none = openStore(path, false);  // no passphrase at all: an unreadable file, not a plain store
    CHECK(none.status == StoreStatus::Damaged);
    CHECK(readFile(path) == before);

    auto right = openStore(path, false, test::kTestPassphrase);
    REQUIRE(right.status == StoreStatus::Opened);
    CHECK(right.db->queryInt("SELECT count(*) FROM probe").value_or(-1) == 1);
    right.db.reset();
    fs::remove_all(dir);
}

TEST_CASE("TC-Sec-01 a plain store is refused until the user agrees to encrypt it, and then keeps its data") {
    auto dir = test::uniqueTempDir();
    const auto path = (dir / "old.db").string();
    const std::string marker = "ZebulonQuuxMarker 9876543210";
    {
        auto s = openStore(path, true);  // an old, unencrypted store
        REQUIRE(s.status == StoreStatus::Opened);
        Transaction tx(*s.db);
        s.db->exec("CREATE TABLE probe (v TEXT); INSERT INTO probe VALUES ('" + marker + "');");
        tx.commit();
    }
    REQUIRE(storeFileIsPlain(path));
    const std::string before = readFile(path);

    auto refused = openStore(path, false, test::kTestPassphrase);  // encrypting was not asked for
    CHECK(refused.status == StoreStatus::NotEncrypted);
    CHECK(readFile(path) == before);

    auto upgraded = openStore(path, false, test::kTestPassphrase, true);
    REQUIRE(upgraded.status == StoreStatus::Opened);
    CHECK(upgraded.encryptedNow);
    upgraded.db.reset();
    CHECK_FALSE(storeFileIsPlain(path));
    CHECK(readFile(path).find(marker) == std::string::npos);

    auto again = openStore(path, false, test::kTestPassphrase);
    REQUIRE(again.status == StoreStatus::Opened);
    CHECK_FALSE(again.encryptedNow);
    CHECK(again.db->queryInt("SELECT count(*) FROM probe").value_or(-1) == 1);
    again.db.reset();
    fs::remove_all(dir);
}

TEST_CASE("TC-Sec-01 the passphrase rule, and a secret is wiped after use") {
    std::string reason;
    CHECK_FALSE(isStorePassphraseAcceptable("short", reason));
    CHECK_FALSE(reason.empty());
    CHECK(isStorePassphraseAcceptable("eight-ch", reason));
    CHECK(isStorePassphraseAcceptable(std::string(128, 'x'), reason));
    CHECK_FALSE(isStorePassphraseAcceptable(std::string(129, 'x'), reason));

    std::string secret = "my-secret-passphrase";
    wipe(secret);
    CHECK(secret.empty());
}

#ifdef _WIN32
TEST_CASE("TC-Sec-06 on Windows the new store and its lock file name only the current user") {
    test::TempStore s;
    REQUIRE(s.store.status == StoreStatus::Opened);
    CHECK(onlyCurrentUserHasAccess(s.path));
    CHECK(onlyCurrentUserHasAccess(s.path + ".lock"));
}

TEST_CASE("TC-Sec-06 an existing store with open permissions is tightened") {
    auto dir = test::uniqueTempDir();
    const auto path = (dir / "old.db").string();
    { std::ofstream(path, std::ios::binary) << "x"; }  // an ordinary file, inherited permissions
    CHECK_FALSE(onlyCurrentUserHasAccess(path));
    std::string err;
    CHECK(restrictToCurrentUser(path, false, err));
    CHECK(onlyCurrentUserHasAccess(path));
    fs::remove_all(dir);
}
#endif

TEST_CASE("data committed in one session is there after a restart") {
    auto dir = test::uniqueTempDir();
    const auto path = (dir / "persist.db").string();
    {
        std::string err;
        auto lock = StoreLock::acquire(canonicalStorePath(path), err);
        auto s = openStore(path, true);
        REQUIRE(s.status == StoreStatus::Opened);
        Transaction tx(*s.db);
        s.db->exec("CREATE TABLE probe (v TEXT); INSERT INTO probe VALUES ('kept');");
        tx.commit();
    }
    {
        std::string err;
        auto lock = StoreLock::acquire(canonicalStorePath(path), err);
        // Opened directly: the foundation schema has no tables yet, so openStore
        // would (correctly) refuse a version-0 file that holds a foreign table.
        Database db(path, Database::Mode::ReadWrite);
        Statement st = db.prepare("SELECT v FROM probe");
        REQUIRE(st.step());
        CHECK(st.getText(0) == "kept");
    }
    fs::remove_all(dir);
}

TEST_CASE("a transaction that is not committed leaves nothing behind") {
    test::TempStore s;
    s.db().exec("CREATE TABLE probe (v INTEGER)");
    {
        Transaction tx(s.db());
        s.db().exec("INSERT INTO probe VALUES (1)");
        // no commit: the destructor rolls back
    }
    CHECK(s.count("SELECT count(*) FROM probe") == 0);

    s.db().setFaultHook([](const char* point) {
        if (std::string(point) == "before_commit") throw InjectedFault(point);
    });
    try {
        Transaction tx(s.db());
        s.db().exec("INSERT INTO probe VALUES (2)");
        tx.commit();
    } catch (const InjectedFault&) {
    }
    s.db().setFaultHook(nullptr);
    CHECK(s.count("SELECT count(*) FROM probe") == 0);

    {
        Transaction tx(s.db());
        s.db().exec("INSERT INTO probe VALUES (3)");
        tx.commit();
    }
    CHECK(s.count("SELECT count(*) FROM probe") == 1);
}

TEST_CASE("a damaged store is refused and its bytes are not changed") {
    auto dir = test::uniqueTempDir();
    const auto path = (dir / "damaged.db").string();
    {
        std::ofstream out(path, std::ios::binary);
        out << "this is not a database file, just some text that is long enough to look like a header....";
        out << std::string(4096, 'x');
    }
    const std::string before = readFile(path);
    auto r = openStore(path, false);
    CHECK(r.status == StoreStatus::Damaged);
    CHECK(r.message.find("has not been changed") != std::string::npos);
    CHECK(readFile(path) == before);
    fs::remove_all(dir);
}

TEST_CASE("a store with a newer schema version is refused and not changed") {
    auto dir = test::uniqueTempDir();
    const auto path = (dir / "newer.db").string();
    {
        Database db(path, Database::Mode::ReadWriteCreate);
        db.exec("CREATE TABLE something (x INTEGER)");
        db.exec("PRAGMA user_version = " + std::to_string(schema::currentVersion() + 1));
    }
    const std::string before = readFile(path);
    auto r = openStore(path, false);
    CHECK(r.status == StoreStatus::UnsupportedVersion);
    CHECK(readFile(path) == before);
    fs::remove_all(dir);
}

TEST_CASE("a foreign SQLite database (version 0 with tables) is refused") {
    auto dir = test::uniqueTempDir();
    const auto path = (dir / "foreign.db").string();
    {
        Database db(path, Database::Mode::ReadWriteCreate);
        db.exec("CREATE TABLE customers (id INTEGER)");
    }
    auto r = openStore(path, false);
    CHECK(r.status == StoreStatus::Damaged);
    fs::remove_all(dir);
}

TEST_CASE("a second instance cannot take the store lock (also through another path)") {
    auto dir = test::uniqueTempDir();
    const auto path = (dir / "locked.db").string();
    std::string err;
    auto first = StoreLock::acquire(canonicalStorePath(path), err);
    REQUIRE(first);
    std::string err2;
    auto second = StoreLock::acquire(canonicalStorePath(path), err2);
    CHECK_FALSE(second);
    CHECK(err2.find("already in use") != std::string::npos);
    fs::create_directories(dir / "sub");
    const auto alias = (dir / "sub" / ".." / "locked.db").string();
    auto third = StoreLock::acquire(canonicalStorePath(alias), err2);
    CHECK_FALSE(third);
    first.reset();
    auto again = StoreLock::acquire(canonicalStorePath(path), err2);
    CHECK(again);  // released when the holder ends
    again.reset();
    fs::remove_all(dir);
}
