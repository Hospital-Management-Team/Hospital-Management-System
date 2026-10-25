#include "data/store.hpp"

#include <filesystem>
#include <system_error>

#include "data/schema.hpp"
#include "platform/store_lock.hpp"

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
}  // namespace

bool storeFileExists(const std::string& utf8Path) {
    std::error_code ec;
    return fs::exists(fromUtf8(utf8Path), ec);
}

std::string checkIntegrity(Database& db) {
    {
        Statement st = db.prepare("PRAGMA quick_check");
        if (!st.step()) return "storage check returned no result";
        const std::string first = st.getText(0);
        if (first != "ok") return "storage check: " + first;
    }
    {
        Statement st = db.prepare("PRAGMA foreign_key_check");
        if (st.step()) return "reference check: table " + st.getText(0) + " has a broken reference";
    }
    return {};
}

StoreOpenResult openStore(const std::string& utf8Path, bool createIfMissing) {
    StoreOpenResult result;
    const bool exists = storeFileExists(utf8Path);

    if (!exists && !createIfMissing) {
        result.status = StoreStatus::NotFound;
        result.message = "No data store was found at " + utf8Path + ".";
        return result;
    }

    if (!exists) {
        std::string err;
        if (!createOwnerOnlyFile(utf8Path, err)) {
            result.status = StoreStatus::Failed;
            result.message = "Could not create the data store: " + err;
            return result;
        }
        result.created = true;
    }

    std::unique_ptr<Database> db;
    try {
        db = std::make_unique<Database>(utf8Path, Database::Mode::ReadWrite);
        db->exec("PRAGMA foreign_keys = ON");

        // Read-only checks first: a damaged store must never be modified.
        const std::string problem = checkIntegrity(*db);
        if (!problem.empty()) {
            result.status = StoreStatus::Damaged;
            result.message = "The data store failed its integrity check (" + problem +
                             "). It has not been changed. Restore it from a backup.";
            return result;
        }

        const int version = static_cast<int>(db->queryInt("PRAGMA user_version").value_or(0));
        const auto tables = db->queryInt("SELECT count(*) FROM sqlite_master WHERE type = 'table'").value_or(0);
        result.schemaVersion = version;

        if (version > schema::currentVersion()) {
            result.status = StoreStatus::UnsupportedVersion;
            result.message = "The data store has schema version " + std::to_string(version) +
                             ", but this program supports up to version " +
                             std::to_string(schema::currentVersion()) +
                             ". It has not been changed. Use a newer build of the application.";
            return result;
        }
        if (version == 0 && tables > 0) {
            result.status = StoreStatus::Damaged;
            result.message = "The file is a database but not an HMS data store. It has not been changed.";
            return result;
        }

        // Durability settings (SAD ADR-002): rollback journal, full sync.
        db->exec("PRAGMA journal_mode = DELETE");
        db->exec("PRAGMA synchronous = FULL");

        if (version < schema::currentVersion()) {
            schema::migrate(*db, version);
            result.migrated = version > 0;
            result.schemaVersion = schema::currentVersion();
        }
    } catch (const DbError& e) {
        // SQLITE_NOTADB and similar: the file is not a readable database.
        result.status = StoreStatus::Damaged;
        result.message = std::string("The data store cannot be read (") + e.what() +
                         "). It has not been changed. Restore it from a backup.";
        return result;
    }

    result.status = StoreStatus::Opened;
    result.db = std::move(db);
    return result;
}

}  // namespace hms
