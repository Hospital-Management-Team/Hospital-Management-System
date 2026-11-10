# Sprint 2 – QA Verification Report

## 1. Project details

- Project: Hospital Management System
- Sprint: Sprint 2
- QA owner: Harsh Raj
- Role: QA Lead

## 2. Build verification

The application was built with CMake.

| Check | Result |
|---|---|
| CMake build | Passed |
| CTest execution | 1/1 test passed (the whole automated suite is one CTest test) |
| Automated test cases | 246 of 246 passed, 0 failed, 0 skipped |
| Assertions | 4,879 of 4,879 passed |

The test output is in `reports/unit-test-output.txt` and `reports/unit-test-report.xml`. The coverage reports are `reports/code-coverage.txt` and `reports/branch-coverage.txt`.

## 3. Functional testing

The QA Lead checked these functions in the application:

| Function | Result |
|---|---|
| Administrator account creation | Passed |
| Administrator authentication | Passed |
| Staff account creation | Passed |
| Receptionist authentication | Passed |
| Patient registration | Passed |
| Patient search by UHID | Passed |
| Patient record retrieval | Passed |
| Appointment booking | Passed |
| Appointment availability validation | Passed |
| Receptionist menu and access | Passed |

The Test Plan cases were also run, one by one, in the system validation run: see `reports/system-validation-report.md`.

## 4. Security verification

- Staff authentication with username and password.
- Role-based access for each staff role.
- Administrator account management.
- Password complexity rules.
- Audit log (TC-Sec-05), covered by the automated tests.

The system validation run found two security failures, TC-Sec-01 (patient data stored in plain text) and TC-Sec-06 (data store file readable by other Windows users). They are listed in section 6.

## 5. Non-functional requirements

- The application builds and starts.
- A patient store with 100,000 records was created with the `hms_seed` tool. The timings of the patient searches at that size are in the system validation run (HMS-NF-001, TC-Perf-01). Booking was not timed in that run.
- Database integrity is checked every time the data store is opened.
- The usability test with three new users (TC-UX-01) has not been run.

## 6. Open findings

These are taken from `reports/system-validation-report.md`; each difference from the submitted documents is recorded in `deviation-log.md`.

| Case | Result | Finding |
|---|---|---|
| TC-Sec-01 | Fail | Patient data is stored in plain text (D-06). |
| TC-Sec-06 | Fail | The data store is readable by other Windows users (D-07). |
| TC-Perf-01 | Partial | Searches were timed at 100,000 patients; booking was not. |
| TC-Admin-01 | Partial | Doctors' departments were missing from the department list (D-59); booking and billing were not checked. |
| TC-Admin-02 | Not run | The Admin settings screen was not built (D-60). |
| TC-UX-01 | Not run | Needs three new users. |

The Test Plan's exit criterion is that 100% of the planned test cases are executed; it is still open in D-32.

## 7. Conclusion

The build and the automated test suite passed. The application is not yet ready for sign-off: two security cases failed and three cases are partly passed or not run (section 6).

**Overall QA status: automated tests passed; manual validation findings are open.**
