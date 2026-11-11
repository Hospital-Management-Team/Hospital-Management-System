# Sprint 3 – Work Summary

## 1. Project details

- Project: Hospital Management System (Team 2)
- Sprint: Sprint 3
- Period: 2 November 2026 to 6 November 2026
- Team: Harsh Raj, Dasari Vinay, Diya J Marar, Chris Khushnam

## 2. Summary

The remaining feature modules were merged: consultation timing (#5), medical history, visit history and the audit log view (#6), laboratory results and pharmacy inventory (#8), and the Admin reports, staff accounts and master data (#9). The test suite, coverage reports and the Sprint 2 QA verification report were added, and a manual system validation run was made against the Test Plan.

At the end of the period the automated suite had 246 test cases and 4,879 assertions, all passing. The system validation run found two failed cases, two partly passed cases and two cases not run; they are listed in section 5.

## 3. Work completed

| Pull request | Merged | Author | Issue | What it added |
|---|---|---|---|---|
| #27 | 2 Nov | Chris Khushnam | #5 | A consultation can no longer be recorded before its appointment starts (D-47). |
| #28 | 2 Nov | Dasari Vinay | #6 | Versioned medical history, allergies and current medications; only a doctor with a booked or completed appointment with the patient can open it (22 files, 913 lines added; D-48, D-49). |
| #29 | 2 Nov | Diya J Marar | #6 | Visit history restricted to doctors with a booked or completed appointment; a read-only audit log view for the Administrator, with its refusals audited (D-49, D-50). |
| #30 | 2 Nov | Chris Khushnam | #8 | Lab Technician and Pharmacist roles; lab test orders from the consultation and result entry with revisions; pharmacy stock, dispensing against prescription lines and the low-stock alert (29 files, 2,682 lines added; D-51 to D-56). |
| #31 | 4 Nov | Chris Khushnam | #9 | Admin summary reports, staff account management (deactivate, reactivate, unlock, new password) and master data: departments, rooms and the doctor-department mapping (18 files, 1,727 lines added; D-57 to D-59). |
| #32 | 5 Nov | Diya J Marar | – | Unit test output, code coverage and branch coverage reports in `docs/reports/`. |
| #33 | 5 Nov | Harsh Raj | – | Sprint 2 QA verification report (`docs/QA_Sprint2_Report.md`). |

Harsh Raj reviewed and merged every pull request above.

Every change that differs from the submitted documents has a row in `docs/deviation-log.md`: D-47 to D-59 were added in this period. D-60 (Admin settings, HMS-F-052) was recorded for #9 as not built yet.

## 4. Testing

| Check | Result |
|---|---|
| Automated test cases | 246 of 246 passed, 0 failed, 0 skipped |
| Assertions | 4,879 of 4,879 passed |
| Line coverage | 87% (5,765 of 6,577 lines) |
| Branch coverage | 69% (8,591 of 12,418 branches) |
| Build | CI builds with GCC (AddressSanitizer and UBSan), Clang and MSVC on every push and pull request |

The reports are in `docs/reports/`: `unit-test-output.txt`, `unit-test-report.xml`, `code-coverage.txt` and `branch-coverage.txt`.

A system validation run was made on 5 November against the 18 Test Plan cases and the 11 SRS traceability cases (tester: Dasari Vinay). The Sprint 2 QA verification report (Harsh Raj) summarises its findings.

## 5. Findings open at the end of the sprint

| Case | Result | Finding |
|---|---|---|
| TC-Sec-01 | Fail | Patient data was stored in plain text (D-06). |
| TC-Sec-06 | Fail | The data store was readable by other Windows users (D-07). |
| TC-Perf-01 | Partial | Searches were timed at 100,000 patients; booking was not. |
| TC-Admin-01 | Partial | Doctors' departments were missing from the department list (D-59). |
| TC-Admin-02 | Not run | The Admin settings screen was not built (D-60). |
| TC-UX-01 | Not run | Needs three testers (to be three members of the team, D-32). |

Also open at the end of the sprint: D-22 (no Hospital Management Service controller, as in SAD 3.3) and D-32 (the Test Plan's exit criterion of 100% of test cases executed) needed a team decision. Both were decided on 10 November and closed in the deviation log.

## 6. Carried over

- Fix the two failed cases and the two partly passed cases above, and build the Admin settings screen (HMS-F-052).
- Edit a patient's details (HMS-F-005): there was no screen for it yet.
- Run the usability test (TC-UX-01), with three members of the team as the testers (D-32).
- Decide D-22 and D-32 (decided on 10 November).

The fixes and the missing screens were done in pull request #34 (10 November). TC-UX-01 has not been run.
