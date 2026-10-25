// Store foundation (issue #1): creation, durability settings, persistence
// across restarts, schema-version refusal, damaged-store refusal, per-store
// lock and all-or-nothing transactions.
#include <doctest.h>

#include <fstream>
#include <iterator>

#include "data/schema.hpp"
#include "test_support.hpp"

#ifndef _WIN32
#include <sys/stat.h>
#endif

using namespace hms;
namespace fs = std::filesystem;

namespace {
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
