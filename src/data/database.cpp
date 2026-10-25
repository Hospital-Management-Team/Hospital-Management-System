#include "data/database.hpp"

#include <sqlite3.h>

namespace hms {

namespace {
[[noreturn]] void fail(sqlite3* db, int rc, const std::string& context) {
    std::string msg = context + ": " + (db ? sqlite3_errmsg(db) : sqlite3_errstr(rc));
    throw DbError(rc, msg);
}
}  // namespace

// ---------------------------------------------------------------- Statement

Statement::Statement(sqlite3* db, const std::string& sql) : db_(db) {
    const int rc = sqlite3_prepare_v2(db_, sql.c_str(), static_cast<int>(sql.size()), &stmt_, nullptr);
    if (rc != SQLITE_OK) fail(db_, rc, "prepare");
}

Statement::~Statement() {
    if (stmt_) sqlite3_finalize(stmt_);
}

Statement::Statement(Statement&& other) noexcept : db_(other.db_), stmt_(other.stmt_) { other.stmt_ = nullptr; }

Statement& Statement::bind(int index, std::int64_t value) {
    const int rc = sqlite3_bind_int64(stmt_, index, value);
    if (rc != SQLITE_OK) fail(db_, rc, "bind");
    return *this;
}

Statement& Statement::bind(int index, const std::string& value) {
    const int rc = sqlite3_bind_text(stmt_, index, value.c_str(), static_cast<int>(value.size()), SQLITE_TRANSIENT);
    if (rc != SQLITE_OK) fail(db_, rc, "bind");
    return *this;
}

Statement& Statement::bind(int index, const std::optional<std::string>& value) {
    return value ? bind(index, *value) : bindNull(index);
}

Statement& Statement::bind(int index, const std::optional<std::int64_t>& value) {
    return value ? bind(index, *value) : bindNull(index);
}

Statement& Statement::bindNull(int index) {
    const int rc = sqlite3_bind_null(stmt_, index);
    if (rc != SQLITE_OK) fail(db_, rc, "bind");
    return *this;
}

bool Statement::step() {
    const int rc = sqlite3_step(stmt_);
    if (rc == SQLITE_ROW) return true;
    if (rc == SQLITE_DONE) return false;
    fail(db_, rc, "step");
}

void Statement::run() {
    while (step()) {
    }
}

void Statement::reset() {
    sqlite3_reset(stmt_);
    sqlite3_clear_bindings(stmt_);
}

std::int64_t Statement::getInt64(int col) const { return sqlite3_column_int64(stmt_, col); }

std::string Statement::getText(int col) const {
    const auto* p = sqlite3_column_text(stmt_, col);
    const int n = sqlite3_column_bytes(stmt_, col);
    return p ? std::string(reinterpret_cast<const char*>(p), static_cast<std::size_t>(n)) : std::string();
}

std::optional<std::string> Statement::getOptionalText(int col) const {
    if (isNull(col)) return std::nullopt;
    return getText(col);
}

std::optional<std::int64_t> Statement::getOptionalInt64(int col) const {
    if (isNull(col)) return std::nullopt;
    return getInt64(col);
}

bool Statement::isNull(int col) const { return sqlite3_column_type(stmt_, col) == SQLITE_NULL; }

// ----------------------------------------------------------------- Database

Database::Database(const std::string& utf8Path, Mode mode) {
    int flags = SQLITE_OPEN_READWRITE | SQLITE_OPEN_NOMUTEX;
    if (mode == Mode::ReadWriteCreate) flags |= SQLITE_OPEN_CREATE;
    const int rc = sqlite3_open_v2(utf8Path.c_str(), &db_, flags, nullptr);
    if (rc != SQLITE_OK) {
        std::string msg = "open " + utf8Path + ": " + (db_ ? sqlite3_errmsg(db_) : sqlite3_errstr(rc));
        if (db_) sqlite3_close(db_);
        db_ = nullptr;
        throw DbError(rc, msg);
    }
    sqlite3_extended_result_codes(db_, 1);
}

Database::~Database() {
    if (db_) sqlite3_close_v2(db_);
}

void Database::exec(const std::string& sql) {
    char* err = nullptr;
    const int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &err);
    if (rc != SQLITE_OK) {
        std::string msg = err ? err : sqlite3_errstr(rc);
        sqlite3_free(err);
        throw DbError(rc, "exec: " + msg);
    }
}

Statement Database::prepare(const std::string& sql) { return Statement(db_, sql); }

std::int64_t Database::lastInsertRowId() const { return sqlite3_last_insert_rowid(db_); }

bool Database::inTransaction() const { return sqlite3_get_autocommit(db_) == 0; }

std::optional<std::int64_t> Database::queryInt(const std::string& sql) {
    Statement st = prepare(sql);
    if (!st.step()) return std::nullopt;
    return st.getInt64(0);
}

// -------------------------------------------------------------- Transaction

Transaction::Transaction(Database& db) : db_(db) {
    db_.exec("BEGIN IMMEDIATE");
    active_ = true;
}

Transaction::~Transaction() { rollback(); }

void Transaction::commit() {
    if (!active_) throw DbError(SQLITE_MISUSE, "commit without an active transaction");
    db_.faultPoint("before_commit");
    try {
        db_.exec("COMMIT");
    } catch (const DbError& e) {
        // SQLite may or may not have written the change. Report it as uncertain.
        active_ = db_.inTransaction();
        throw CommitUncertain(e.code(), e.what());
    }
    active_ = false;
    db_.faultPoint("after_commit");
}

void Transaction::rollback() {
    if (!active_) return;
    active_ = false;
    if (db_.inTransaction()) {
        try {
            db_.exec("ROLLBACK");
        } catch (...) {
            // Nothing more can be done here; SQLite rolls back on close.
        }
    }
}

}  // namespace hms
