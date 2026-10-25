#pragma once
// Thin RAII wrapper over the SQLite C API (Data Access Module, SAD 3.4).
// The Data Access Module runs statements and enforces store constraints; it
// decides no business rule. Business modules own their transactions
// (SAD 4.4, "Ownership").

#include <cstdint>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>

struct sqlite3;
struct sqlite3_stmt;

namespace hms {

// Any SQLite failure. Business modules catch it and roll back.
class DbError : public std::runtime_error {
public:
    DbError(int code, const std::string& what) : std::runtime_error(what), code_(code) {}
    int code() const { return code_; }

private:
    int code_;
};

// COMMIT failed: whether the change reached the disk is not known
// (SRS 2.5 outcome 3, "Outcome uncertain").
class CommitUncertain : public DbError {
public:
    using DbError::DbError;
};

// Thrown by the test fault hook to simulate a failure at a named point
// (STP 5.2 "failure injection at defined transaction boundaries").
class InjectedFault : public std::runtime_error {
public:
    explicit InjectedFault(const std::string& point) : std::runtime_error("injected fault at " + point) {}
};

class Statement {
public:
    Statement(sqlite3* db, const std::string& sql);
    ~Statement();
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;
    Statement(Statement&& other) noexcept;
    Statement& operator=(Statement&&) = delete;

    Statement& bind(int index, std::int64_t value);
    Statement& bind(int index, int value) { return bind(index, static_cast<std::int64_t>(value)); }
    Statement& bind(int index, const std::string& value);
    Statement& bind(int index, const char* value) { return bind(index, std::string(value)); }
    Statement& bind(int index, const std::optional<std::string>& value);
    Statement& bind(int index, const std::optional<std::int64_t>& value);
    Statement& bindNull(int index);

    // Returns true while a row is available; false when done.
    bool step();
    // Runs a statement that returns no rows.
    void run();
    void reset();

    std::int64_t getInt64(int col) const;
    std::string getText(int col) const;
    std::optional<std::string> getOptionalText(int col) const;
    std::optional<std::int64_t> getOptionalInt64(int col) const;
    bool isNull(int col) const;

private:
    sqlite3* db_;
    sqlite3_stmt* stmt_ = nullptr;
};

class Database {
public:
    enum class Mode { ReadWrite, ReadWriteCreate };

    Database(const std::string& utf8Path, Mode mode);
    ~Database();
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    void exec(const std::string& sql);
    Statement prepare(const std::string& sql);
    std::int64_t lastInsertRowId() const;
    bool inTransaction() const;

    // Returns the first column of the first row, or nullopt if no row.
    std::optional<std::int64_t> queryInt(const std::string& sql);

    // Test seam: called at named points ("before_commit", ...). A test may
    // throw InjectedFault from it. Unset in production.
    void setFaultHook(std::function<void(const char* point)> hook) { faultHook_ = std::move(hook); }
    void faultPoint(const char* point) const {
        if (faultHook_) faultHook_(point);
    }

    sqlite3* handle() { return db_; }

private:
    sqlite3* db_ = nullptr;
    std::function<void(const char*)> faultHook_;
};

// BEGIN IMMEDIATE ... COMMIT, rolled back by the destructor unless committed.
class Transaction {
public:
    explicit Transaction(Database& db);
    ~Transaction();
    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;

    void commit();    // throws CommitUncertain if COMMIT fails
    void rollback();  // safe to call more than once

private:
    Database& db_;
    bool active_ = false;
};

}  // namespace hms
