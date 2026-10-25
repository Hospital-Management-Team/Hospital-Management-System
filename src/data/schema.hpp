#pragma once
// Store schema and its numbered migrations.
//
// The schema version is kept in PRAGMA user_version (SAD ADR-002). Each
// later sprint adds tables by appending a Migration with the next version
// number; existing migrations are never edited once merged, so a data store
// created by an earlier build can be brought forward step by step.
// (The v11 working-draft SRS HMS-F-053(12) defers migration; forward
// migration is listed in docs/deviation-log.md.)

#include <string>
#include <vector>

#include "data/database.hpp"

namespace hms::schema {

struct Migration {
    int version;              // user_version after this step
    std::string description;
    std::string sql;
};

const std::vector<Migration>& migrations();

// Highest version this build understands.
int currentVersion();

// Applies every migration after `fromVersion`, each in its own transaction.
void migrate(Database& db, int fromVersion);

}  // namespace hms::schema
