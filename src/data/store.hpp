#pragma once
// Opening the data store (first part of HMS-F-053, start-up states).
//
// Implemented now:
//   - a missing store is created only after the user confirms (rule 1);
//   - a store that fails the integrity check, or is not an HMS store, is
//     refused and never modified (rule 2);
//   - a store with a newer schema version than this build is refused (12);
//   - an older schema version is migrated forward, one step at a time.
// Later: installation marker, first-run Admin setup, no-Admin rule,
// Recovery mode, backup and restore.

#include <memory>
#include <string>

#include "data/database.hpp"

namespace hms {

enum class StoreStatus {
    Opened,
    NotFound,            // no store file and creation not allowed
    Damaged,             // integrity check failed, unreadable, or not an HMS store
    UnsupportedVersion,  // schema version newer than this build
    WrongPassphrase,     // an encrypted store that this passphrase does not open (or a damaged one)
    NotEncrypted,        // a plain store, and encrypting it was not allowed by the caller
    Failed               // could not create or open for another reason
};

struct StoreOpenResult {
    StoreStatus status = StoreStatus::Failed;
    std::string message;               // explanation for the user
    std::unique_ptr<Database> db;      // set only when status == Opened
    int schemaVersion = 0;
    bool created = false;              // a new, empty store was created
    bool migrated = false;             // an older schema was brought forward
    bool encryptedNow = false;         // a plain store was encrypted by this call
};

bool storeFileExists(const std::string& utf8Path);

// True when the file starts with the plain SQLite header, i.e. it is not encrypted. An encrypted
// store looks like random bytes, and a missing or empty file is not "plain".
bool storeFileIsPlain(const std::string& utf8Path);

// Passphrase rule for the store (HMS-NF-003): 8 to 128 characters. Fills `reason` when refused.
bool isStorePassphraseAcceptable(const std::string& passphrase, std::string& reason);

// Overwrites the text so a passphrase does not stay in memory after it is used.
void wipe(std::string& secret);

// The caller must already hold the StoreLock for this path.
//
// `passphrase` encrypts a new store and opens an encrypted one (patient and medical data are never
// stored in plain text, HMS-NF-003). A wrong one gives WrongPassphrase and changes nothing.
// A plain, unencrypted file is refused (NotEncrypted) unless `encryptPlainStore` is true, in which
// case it is checked and then encrypted in place with `passphrase`.
// An empty `passphrase` opens a plain store: tests and the seed tool only, never the application.
StoreOpenResult openStore(const std::string& utf8Path, bool createIfMissing, const std::string& passphrase = {},
                          bool encryptPlainStore = false);

// Integrity check (SRS 1.4 checks (1) and (2)): storage engine check plus
// foreign-key check. Returns an empty string when healthy.
std::string checkIntegrity(Database& db);

}  // namespace hms
