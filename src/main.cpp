#include <iostream>
#include <sqlite3.h>

int main() {
    sqlite3* db = nullptr;

    if (sqlite3_open("database/hospital.db", &db) != SQLITE_OK) {
        std::cerr << "Database error: " << sqlite3_errmsg(db)
                  << std::endl;
        sqlite3_close(db);
        return 1;
    }

    const char* sql = R"(
        CREATE TABLE IF NOT EXISTS patients (
            patient_id INTEGER PRIMARY KEY AUTOINCREMENT,
            uhid TEXT NOT NULL UNIQUE,
            full_name TEXT NOT NULL,
            age INTEGER NOT NULL CHECK(age >= 0),
            phone TEXT NOT NULL UNIQUE
        );
    )";

    char* errorMessage = nullptr;
    int result = sqlite3_exec(db, sql, nullptr, nullptr, &errorMessage);

    if (result != SQLITE_OK) {
        std::cerr << "Table creation failed: "
                  << errorMessage << std::endl;
        sqlite3_free(errorMessage);
        sqlite3_close(db);
        return 1;
    }

    std::cout << "Hospital Management System" << std::endl;
    std::cout << "SQLite connected successfully!" << std::endl;
    std::cout << "Patients table created successfully!" << std::endl;

    sqlite3_close(db);
    return 0;
}
