# Third-party code

Both libraries are vendored (copied into the repository) so the project builds on Windows and Linux with nothing to install except a C++17 compiler and CMake.

| Library | Version | Files | Licence | Used for |
|---|---|---|---|---|
| SQLite | 3.50.4 (amalgamation) | `sqlite/sqlite3.c`, `sqlite/sqlite3.h` | Public domain | The data store (SAD ADR-002) |
| doctest | 2.4.11 | `doctest/doctest.h` | MIT | Unit and integration tests |

SHA-256 of the vendored files:

```
90b7189566ea710ee18361b25d893f5cda3fcf43033223a16384762f53e99a85  sqlite/sqlite3.c
abd1514e0351f79393d1be882830afdb40a8099e8257f311f0bfdf8486f11bea  sqlite/sqlite3.h
44faa038e9c3f9728efbda143748d01124ea0a27f4bf78f35a15d8fab2e039fb  doctest/doctest.h
```

The SQLite amalgamation was taken unmodified from the `better-sqlite3@12.4.1` npm package, which ships the official 3.50.4 amalgamation from sqlite.org. To replace it, download `sqlite-amalgamation-3XXXXXX.zip` from https://www.sqlite.org/download.html and copy `sqlite3.c` and `sqlite3.h` here. doctest came from https://github.com/doctest/doctest (tag v2.4.11).
