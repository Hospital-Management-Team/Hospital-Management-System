#include "data/store.hpp"

#include <cstring>
#include <filesystem>
#include <fstream>
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

bool storeFileIsPlain(const std::string& utf8Path) {
    std::ifstream in(fromUtf8(utf8Path), std::ios::binary);
    char header[16] = {};
    if (!in.read(header, sizeof(header))) return false;
    return std::memcmp(header, "SQLite format 3", 15) == 0 && header[15] == '\0';
}

bool isStorePassphraseAcceptable(const std::string& passphrase, std::string& reason) {
    if (passphrase.size() < 8) {
        reason = "The passphrase must be at least 8 characters.";
        return false;
    }
    if (passphrase.size() > 128) {
        reason = "The passphrase must be at most 128 characters.";
        return false;
    }
    return true;
}

void wipe(std::string& secret) {
    volatile char* p = secret.empty() ? nullptr : &secret[0];
    for (std::size_t i = 0; i < secret.size(); ++i) p[i] = 0;
    secret.clear();
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

StoreOpenResult openStore(const std::string& utf8Path, bool createIfMissing, const std::string& passphrase,
                          bool encryptPlainStore) {
    StoreOpenResult result;
    const bool exists = storeFileExists(utf8Path);
    const bool plainOnDisk = exists && storeFileIsPlain(utf8Path);

    // A plain store is never used as it is by the application: it is encrypted first, or refused.
    if (plainOnDisk && !passphrase.empty() && !encryptPlainStore) {
        result.status = StoreStatus::NotEncrypted;
        result.message = "The data store at " + utf8Path +
                         " is not encrypted. Patient data must not be stored in plain text. It has not been changed.";
        return result;
    }

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
        // A plain file being encrypted is opened plain first and keyed once its checks have passed.
        db = std::make_unique<Database>(utf8Path, Database::Mode::ReadWrite, plainOnDisk ? std::string() : passphrase);
        if (exists && !plainOnDisk && !result.created) {
            // The first read tells a right passphrase from a wrong one (or from a damaged file).
            try {
                db->queryInt("SELECT count(*) FROM sqlite_master");
            } catch (const DbError&) {
                if (passphrase.empty()) throw;  // no passphrase given: an unreadable file, as before
                db.reset();
                result.status = StoreStatus::WrongPassphrase;
                result.message = "The passphrase is wrong, or the data store is damaged. It has not been changed.";
                return result;
            }
        }
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

        if (plainOnDisk && !passphrase.empty()) {
            db->rekey(passphrase);  // encrypts the whole file in place (HMS-NF-003)
            result.encryptedNow = true;
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
