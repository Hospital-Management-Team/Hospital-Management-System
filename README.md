# Hospital Management System (Team 2)

A patient record and appointment management system for hospitals: a single-user, single-machine C++17 console application backed by SQLite.

Requirements, design and tests are in [`SRS_Hospital_Management_System_Team2.md`](SRS_Hospital_Management_System_Team2.md), [`Hospital_Management_System_SAD.docx`](Hospital_Management_System_SAD.docx) and [`HMS_Software_Test_Plan.docx`](HMS_Software_Test_Plan.docx).

## Build and run

You need a C++17 compiler and CMake 3.16 or later. SQLite and doctest are included in `third_party/`, so nothing else has to be installed.

**Linux (Ubuntu 24.04)**

```bash
sudo apt install build-essential cmake ninja-build
cmake -S . -B build -G Ninja
cmake --build build
./build/hms                       # data store: database/hospital.db
ctest --test-dir build --output-on-failure
```

**Windows 11 (Visual Studio 2022 with "Desktop development with C++")**

```bat
cmake -S . -B build
cmake --build build --config Debug
build\Debug\hms.exe
ctest --test-dir build -C Debug --output-on-failure
```

Or open the folder in Visual Studio ("Open a local folder") and it configures CMake automatically.

Options: `--store PATH` uses another data store file. To cancel at any prompt, press Ctrl+D on Linux or Ctrl+Z then Enter on Windows.

## Project layout

```
src/app/         main.cpp: start-up, store lock, store opening
src/core/        Result<T> error contract, Clock (replaceable in tests), text and ID helpers
src/data/        SQLite wrapper (Database, Statement, Transaction), schema migrations, store opening
src/platform/    the only OS-specific code: per-store lock, owner-only file creation, UTF-8 console
src/validation/  input layer (SRS 3.1): line checks, DD-MM-YYYY dates, whole numbers, money
src/ui/          ConsoleIO: prompts, re-asking on invalid input, end of input cancels
src/config/      build-time policy values ([TEAM DECISION] items in the SRS)
tests/           doctest suites (run by CI on every push and pull request)
third_party/     SQLite 3.50.4 amalgamation, doctest 2.4.11
```

## Rules for every module

1. **Results:** every business function returns `Result<T>`, which holds a value or one `Error`: Validation (with every bad field named), AccessDenied, NotFound, Conflict, StorageFailure (rolled back) or OutcomeUnknown (commit not confirmed).
2. **Transactions:** the business module owns the transaction. It opens a `Transaction`, applies the rule, and commits. Anything not committed is rolled back automatically.
3. **Time:** time comes from an injected `Clock`. Tests use `FixedClock`.
4. **Schema:** schema changes are new entries appended to `src/data/schema.cpp`. Never edit a merged migration. Older stores upgrade on opening; newer ones are refused unchanged.
5. **Input:** input is checked with the functions in `src/validation/` before it reaches business rules. Each error names the field and the rule.
6. **Build and tests:** add new sources to `HMS_FEATURE_SOURCES` and new tests to `HMS_FEATURE_TEST_SOURCES` in `CMakeLists.txt`.

## Continuous integration

`.github/workflows/ci.yml` builds and runs every test on each push and pull request:
- Ubuntu 24.04 with GCC, plus AddressSanitizer and UBSan;
- Ubuntu 24.04 with Clang;
- Windows with MSVC.

## Team

Harsh Raj (PES2UG24CS184), Dasari Vinay (PES2UG24CS145), Diya J Marar (PES2UG24CS162), Chris Khushnam (PES2UG24CS139). PES University, Software Engineering mini-project.
