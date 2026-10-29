# Hospital Management System (Team 2)

A patient record and appointment management system for hospitals: a single-user, single-machine C++17 console application backed by SQLite.

Requirements, design and tests are in [`SRS_Hospital_Management_System_Team2.md`](SRS_Hospital_Management_System_Team2.md), [`Hospital_Management_System_SAD.docx`](Hospital_Management_System_SAD.docx) and [`HMS_Software_Test_Plan.docx`](HMS_Software_Test_Plan.docx).

## Build and run

You need a C++17 compiler and CMake 3.16 or later. SQLite, Argon2 and doctest are included in `third_party/`, so nothing else has to be installed.

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
src/app/         main.cpp: start-up, store lock, store opening, first-run Administrator setup, login, Admin menu
src/core/        Result<T> error contract, Clock (replaceable in tests), text and ID helpers
src/data/        SQLite wrapper (Database, Statement, Transaction), schema migrations, store opening
src/platform/    the only OS-specific code: per-store lock, owner-only file creation, UTF-8 console, hidden password input
src/validation/  input layer (SRS 3.1): line checks, DD-MM-YYYY dates, whole numbers, money
src/ui/          ConsoleIO: prompts, re-asking on invalid input, end of input cancels
src/config/      build-time policy values ([TEAM DECISION] items in the SRS)
src/patient/     patient registration, duplicate review, search (issue #2)
src/audit/       append-only audit trail, written in the same transaction as each change
src/auth/        Session, role check, staff login, password hashing and policy (issue #3)
src/appointment/ doctor schedules, booking, double-booking prevention, reschedule, cancel, reminders, doctor queue (issue #4)
tools/           hms_seed: synthetic test data and search timings
tests/           doctest suites (run by CI on every push and pull request)
third_party/     SQLite 3.50.4 amalgamation, Argon2 reference implementation, doctest 2.4.11
```

## Rules for every module

1. **Results:** every business function returns `Result<T>`, which holds a value or one `Error`: Validation (with every bad field named), AccessDenied, NotFound, Conflict, StorageFailure (rolled back) or OutcomeUnknown (commit not confirmed).
2. **Transactions:** the business module owns the transaction. It opens a `Transaction`, applies the rule, and commits. Anything not committed is rolled back automatically.
3. **Time:** time comes from an injected `Clock`. Tests use `FixedClock`.
4. **Schema:** schema changes are new entries appended to `src/data/schema.cpp`. Never edit a merged migration. Older stores upgrade on opening; newer ones are refused unchanged.
5. **Input:** input is checked with the functions in `src/validation/` before it reaches business rules. Each error names the field and the rule.
6. **Build and tests:** add new sources to `HMS_FEATURE_SOURCES` and new tests to `HMS_FEATURE_TEST_SOURCES` in `CMakeLists.txt`.

## Features

### Staff login and roles (issue #3)

Only staff log in. Patients are records, not users (SRS 2.3).

1. **First run:** on an empty data store the app asks you to create the Administrator account. Passwords need 8+ characters with an upper-case letter, a lower-case letter, a digit and a symbol.
2. **Login:** everyone, including the Administrator, logs in with a username and password. The role comes from the stored account and is never chosen by the user. After 5 failed attempts the account is locked.
3. **Administrator** can create staff accounts (Doctor, Receptionist, Administrator) and set each doctor's department and weekly hours (issue #4).
4. **Receptionist** registers and searches patients. **Doctor** searches and opens patient records.

Passwords are stored only as salted Argon2id hashes.

Not built yet (see `docs/deviation-log.md`, D-15 to D-18): lockout cooldown, audit entries for login events, account management beyond creating accounts.
Test evidence: `docs/test-results-sprint1.md`.
### Patient registration and search (issue #2)

Log in as a **Receptionist** (see Staff login above) to register patients. The role now comes from the logged-in account (D-01 is closed by #3).

**Register a patient** (HMS-F-001, HMS-F-002)
- Fields: full name, date of birth (or age if it isn't known), gender, address and mobile number. For a child or an elderly patient, a family member's or carer's number is fine. If nobody has a phone, staff type N and confirm "no phone available"; the record then shows "No phone (confirmed at registration)". Never enter a made-up number (D-04).
- Each field is checked as it is typed and asked again if it's wrong. The details are read back for confirmation before saving.
- **Possible duplicates** are shown before anything is saved:
  - *STRONG*: same name, date of birth and gender.
  - *SAME PHONE*: shares the mobile number, for example a family member.
- Staff choose either "same patient", which uses the existing UHID and creates nothing, or "different person". A different person with a STRONG match needs a reason code. The decision is recorded in the audit trail with the patients that were reviewed.
- The new patient gets a UHID such as `HMS-000001`.

**Search** (HMS-F-004): by UHID, by any part of the name (case doesn't matter), by mobile number in any format (everyone who shares the number is returned), or by name and date of birth together (D-24).

**Find a returning patient** (Receptionist, option 4): ask for the mobile number first. The list shows everyone registered with that number; confirm the person by name and date of birth, then open the record. If the patient has no phone or isn't listed, the screen moves on to a name and date-of-birth search. A date of birth estimated from an age matches within one year.

**Doctor** (logs in as a Doctor account): can search and view demographic details only.

**Audit** (HMS-SR-004): every registration and decision is written in the same transaction as the change. The store refuses any edit or deletion of an audit entry.

**Test data:** `./build/hms_seed --store perf.db --count 100000 --bench` creates synthetic patients and times the searches.

### Appointments (issue #4)

**Admin, option 2: doctor's department and weekly hours** (HMS-F-020, minimal). One working window per weekday, typed as `09:00-13:00`, or `off`. A doctor appears on the booking screen only on the days they work. If hours change, booked appointments outside the new hours are kept and the Admin is told how many to reschedule.

**Receptionist**
- **Option 5, book** (HMS-F-010, F-011): patient UHID → date → doctors working that day with their free slots → slot → confirm. You get an appointment number such as `APT-000001`.
  - Slots are 15 minutes, from the start of the doctor's hours. Only slots that haven't started, up to 90 days ahead, are offered.
  - A doctor, and a patient, can have only one booked appointment at any moment. A taken or overlapping slot is refused, and the existing appointment is not changed. The store's unique indexes enforce this too.
- **Option 6, reschedule or cancel** (HMS-F-012): pick the patient's upcoming appointment, then R or C. Changes close **60 minutes** before the start. A cancelled or rescheduled slot is free again at once. A reschedule keeps the old record as Cancelled, linked to the new appointment.
- **Option 7, reminders** (HMS-F-013): booked appointments starting in the next 48 hours, with mobile numbers. A reminder entry is recorded the first time each one is listed (marked NEW). Run it once a day and contact the NEW patients.

**Doctor, option 3: my appointments** (HMS-F-021): the logged-in doctor's patients for a date, in time order.

**Settings:** slot length, cutoff, booking horizon and reminder window are in `AppointmentPolicy` (`src/config/policy.hpp`) until the Admin parameter screen (HMS-F-052) exists (deviation log D-38).

**Audit:** booking, cancelling, rescheduling, reminders and schedule changes are written in the same transaction as the change, with IDs only (`APT-…`, `STAFF-…`). Denied attempts are audited.

Deviations: D-35 to D-41 in `docs/deviation-log.md`. Tests: `tests/test_appointment.cpp` and `tests/test_appointment_console.cpp` (TC-App-01 to 04, TC-Doc-01 and 02).

## Continuous integration

`.github/workflows/ci.yml` builds and runs every test on each push and pull request:
- Ubuntu 24.04 with GCC, plus AddressSanitizer and UBSan;
- Ubuntu 24.04 with Clang;
- Windows with MSVC.

## Team

Harsh Raj (PES2UG24CS184), Dasari Vinay (PES2UG24CS145), Diya J Marar (PES2UG24CS162), Chris Khushnam (PES2UG24CS139). PES University, Software Engineering mini-project.
