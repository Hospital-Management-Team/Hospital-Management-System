# System Validation Report

Tester: Dasari Vinay  
Build: branch `main` at merge commit `5b4873c` (pull request #34), plus the window program `hms_gui` and the fixes made during this run (the pull request that adds the window program), Visual Studio 2026 (MSVC 19.51), CMake 4.4.4, Debug configuration  
Environment: Windows 11 Home, build 26300  
Date: 10 November 2026

How it was run: `hms_gui.exe` (the window program, D-61) ran on a new, empty data store with made-up patients and staff. A script clicked and typed into the window, and every screenshot is a capture of that window only. The staff accounts and the doctors' weekly hours were created beforehand with the console program `hms.exe`, to save time; everything else, including the first start-up, the Administrator's account creation and the staff-account cases, was done in the window. TC-Perf-01 used the project's `hms_seed` tool. TC-Sec-01, TC-Sec-03 and TC-Sec-06 used small read-only checks on the data store file. The images for those checks and for the automated suite are the captured output of the command, drawn as text on a dark background; they are not photographs of a terminal window.

Only tests that were actually run are marked Pass, Partial or Fail. Tests not run are marked Not run, with the reason. Test case IDs are from the Test Plan (Section 13) and the SRS traceability table (Section 8). Where the build differs from the SRS, the row names the entry in `docs/deviation-log.md`.

**Summary:** 29 test cases, 28 run: all 28 passed, 1 not run (TC-UX-01). The Test Plan's own 18 cases: 17 passed, 1 not run. Four additional checks (X-01 to X-04) all passed. The automated suite passed: 281 test cases, 5,316 assertions.

Two defects in the window program were found by this run and fixed in the same change (see "Defects found in this run"). The console program was exercised only through the automated suite and the account set-up described above; the console screens were not walked through again.

## Test cases

### Test Plan cases

| ID | Test | Expected | Actual | Result |
|---|---|---|---|---|
| TC-Auth-01 | Weak and wrong passwords, then a valid login | Wrong password refused without saying which part was wrong; valid login opens the role's menu | First-run Administrator form refused a weak password; "Invalid username or password." for a wrong one; Receptionist login opened the Receptionist menu | Pass |
| TC-Sec-02 | Log in as each of the five roles; a doctor opens a patient with no appointment | Each role gets only its own menu; out-of-role access refused and logged | Five separate menus shown; Dr rao refused the medical history and visit history of a patient he has no appointment with; the audit log holds `MEDICAL_HISTORY_VIEW` and `CONSULTATION_VIEW` entries with outcome DENIED (reason `NO_CARE_RELATIONSHIP`) | Pass |
| TC-Sec-03 | Weak passwords at set-up; read the stored values | Weak passwords refused; salted hash stored, never plaintext | Refused (TC-Auth-01); all six stored values start with `$argon2id$`; two accounts given the same password have different values | Pass |
| TC-Sec-04 | Five wrong passwords, then the correct one, then Admin unlock | Locks on the fifth; correct password refused while locked; unlock works | Locked on the fifth ("Try again in 15 minutes"); correct password refused while locked; Admin saw status "locked" and unlocked; the login after the unlock worked | Pass |
| TC-Reg-01 | Register a patient with a future birth date and an invalid mobile number, then correct both | Each invalid value refused; patient saved with a UHID | Both refused with a message naming the rule; details read back; HMS-000001 issued | Pass |
| TC-Reg-02 | Register a person sharing a mobile number; register an existing patient again | Possible matches shown before saving; a different person gets a new UHID; the same patient gets no new record | SAME PHONE match shown; "different person" issued HMS-000002; a third patient got HMS-000003; registering Asha Kumar again listed STRONG (HMS-000001) and SAME PHONE (HMS-000002) matches | Pass |
| TC-Rec-02 | Search by part of a name, shared mobile number, UHID, name and date of birth, and a name that does not exist | Matching patients found; no match gives a message | 2, 2, 1 and 1 patients found; "No patient found." | Pass |
| TC-App-01 | Book an appointment today and one tomorrow | Appointment saved with a number | APT-000001 (today 19:15) and APT-000002 (tomorrow 10:00) booked | Pass |
| TC-App-02 | Book another patient with the same doctor; book the same patient at the same time with another doctor | A taken slot is not offered; a clash is refused | 19:15 not in Dr mehta's free slots (20 free against 21 for Dr rao); "This patient already has an appointment at 10-11-2026 19:15 with Dr mehta (APT-000001). Nothing was saved." | Pass |
| TC-Doc-03 | Record a consultation before the start, after the start, and a second time | Early and second attempts refused; valid one saved with medicines and tests | Early attempt refused on the first screen, before any form; CON-000001 saved with 1 medicine and 1 test and the appointment Completed; second attempt refused; CON-000002 saved for APT-000001 later | Pass |
| TC-Bill-01 | Bill an unfinished visit; bill a completed visit with a test and a service charge | Unfinished visit refused; itemized bill with the correct total | Refused for APT-000001; BILL-000001 for Rs 850.00 (600 + 150 + 100) | Pass |
| TC-Bill-02 | Part payment, overpayment, final payment | Balance updated; overpayment refused | Rs 300 cash leaves Rs 550; Rs 600 refused ("more than the outstanding balance"); Rs 550 card leaves Rs 0, bill Paid | Pass |
| TC-Bill-03 | Receipt on payment and printed again | Receipt shows the payment and balance | RCPT-000001 and RCPT-000002 issued; RCPT-000001 shown again with the same amounts. The window program shows the receipt on screen only; the text-file save is console-only (D-46, D-61) | Pass |
| TC-Lab-01 | Enter a result, correct it, view as the ordering doctor and as another doctor | Result saved and visible to the treating doctor | Revisions 1 and 2 saved; ordering doctor sees revision 2; other doctor sees nothing | Pass |
| TC-Pharm-01 | Add medicines, dispense, dispense more than prescribed, restock | Stock reduces by the quantity; over-dispensing refused; low-stock alert below the threshold | Three medicines added; LOW STOCK shown at 4 and 8 (threshold 10, D-54); picking Paracetamol against an Amoxicillin line showed a warning and kept Dispense disabled until confirmed (D-53); 4 Amoxicillin dispensed, line complete, stock 0 with a LOW alert; 11 of 10 refused; 10 Paracetamol dispensed, stock 100 to 90; restock took Amoxicillin from 0 to 20 and cleared the alert | Pass |
| TC-Perf-01 | Search and book with 100,000 patients, 100 runs per operation (Debug build, encrypted store) | Search and booking screens: 90th percentile at most 3 s | 90th percentile: UHID 0.24 ms, name 84.7 ms, phone 0.18 ms, booking screen 1.45 ms, find-and-book a slot 9.05 ms. Timings exclude display and typing. A Release build was not timed | Pass |
| TC-Sec-01 | After the full run, check the audit entries and the store file | No plaintext PII or medical data stored | The store file does not start with `SQLite format 3`; entropy 7.9994 bits per byte; none of 24 patient, clinical and account strings found in it. No audit entry mentions patient text (0 matches); entries hold IDs and field names | Pass |
| TC-UX-01 | 3 new receptionists each register a patient after 30 minutes of training | Average at most 5 minutes | Not run: to be done by three members of the team (D-32) | Not run |

### SRS traceability cases

| ID | Test | Expected | Actual | Result |
|---|---|---|---|---|
| TC-Rec-01 | Doctor records a medical history, updates one field, views earlier versions | Update timestamped; prior version kept | MHV-000001 and MHV-000002 saved with time and doctor; only the allergies changed; MHV-000001 listed unchanged | Pass |
| TC-App-03 | Reschedule and cancel outside the cutoff; cancel inside the cutoff (D-38) | Changes work outside the cutoff; slot released; refused inside it | APT-000002 moved to APT-000003 and its slot freed; APT-000003 cancelled; APT-000001 refused ("changes close 60 minutes before the start") | Pass |
| TC-App-04 | Open the reminder list twice | A reminder entry for every booked appointment in the window (48 hours, D-40) | Both appointments listed NEW the first time and "listed before" the second | Pass |
| TC-Doc-01 | Admin sets a doctor's department and weekly hours | Doctor appears on the booking screen | Dr mehta's profile and weekly hours shown; both doctors offered on the booking screen on every working day | Pass |
| TC-Doc-02 | Doctor opens today's queue | Sorted by time; only that doctor's patients | APT-000001 shown with patient and status | Pass |
| TC-Staff-01 | Deactivate an account, try to log in, deactivate own account, reactivate, set a new password | Deactivated account cannot log in; action audited | Login refused; own account refused; reactivation and a new password worked, and the lab technician logged in with it; the actions are in the audit log | Pass |
| TC-Admin-01 | Add a department and a room; view the doctor-department mapping | Changes reflected immediately in booking and billing | Department and room saved and listed; the two doctors' departments (set with their weekly hours, not typed into the department list) appear in the department list with one doctor each (D-59); booking shows "Dr mehta, General Medicine" and the bill shows "Consultation - Dr rao (Cardiology)" | Pass |
| TC-Admin-02 | View and update system parameters | Value changes and is used without a rebuild | The three parameters shown; cutoff changed from 60 to 45 and audited with the old and new value; the change-appointment screen then read "Changes close 45 minutes before"; a cancel 59 minutes before the start, which the earlier 60-minute rule refused (TC-App-03), was accepted (D-60) | Pass |
| TC-Rep-01 | Summary report for 04-11-2026 to 11-11-2026 | Totals match the data entered | 3 patients registered; 4 appointments (0 booked, 2 completed, 2 cancelled); 2 consultations; 1 bill of Rs 850.00; payments Rs 850.00 (cash Rs 300.00, card Rs 550.00); Rs 0.00 unpaid. No No-show column (D-35) | Pass |
| TC-Sec-05 | View the newest entries and the refusals in the audit log | Every action produces a matching entry | Every action of the run is listed; denied logins, the lockout and the unlock appear with reason codes; the settings entry shows the old and new value | Pass |
| TC-Sec-06 | Read the Windows permissions of the store | Access limited to the application's own user; sensitive fields encrypted | A store created by the window program: folder, store file and lock file each list only the running user; the file is encrypted (TC-Sec-01) (D-06, D-07) | Pass |

### Additional checks

These cover behaviour defined in the deviation log or added after the Test Plan, which has no test case ID in the Test Plan or SRS.

| ID | Test | Expected | Actual | Result |
|---|---|---|---|---|
| X-01 | Visit history (HMS-F-006, D-49): treating doctor and a doctor with no appointment | Treating doctor sees the visits; other doctor refused | CON-000001 shown in full to Dr rao; Dr rao refused for the patient he has no appointment with | Pass |
| X-02 | Start-up (HMS-F-053, D-21): no store, decline; damaged store | Nothing created without confirmation; damaged file refused and unchanged | Quit on the new-store question created no folder; a damaged file was refused after three passphrase attempts with the same SHA-256 before and after | Pass |
| X-03 | Single instance (SRS 2.4): second instance on the same store, then close the first | Second instance refused; next start works after the first ends | "Cannot start: the data store is already in use by another instance of the application. Nothing was changed."; the next start reached the passphrase screen | Pass |
| X-04 | Correct a patient's details (HMS-F-005) | Changed field saved after read-back; audit names the field, not the value | Address of HMS-000003 changed after a read-back; the audit entry `PATIENT_UPDATE_DEMOGRAPHICS` lists `address` only | Pass |

Not run, with reason: TC-UX-01 has not been done yet; three members of the team will be the testers (D-32). The Test Plan's exit criterion is the one adopted in D-32.

## Evidence

The tables above give the result of every step. The images show the key steps of each case, not every step.

### Automated suite
Not a Test Plan case; run on the same build for reference. Result: 281 test cases and 5,316 assertions passed, 0 failed. Output of the command, drawn as an image.

<img src="../screenshots/sv-automated-suite.png" alt="Automated suite passed" width="700">

---

### TC-Auth-01: Login
A weak password refused at the first-run Administrator form and a wrong password at login. The menu after a valid login is in the image under TC-Sec-02.

<img src="../screenshots/sv-auth-01b-weak-password-refused.png" alt="TC-Auth-01 weak password refused" width="700">

<img src="../screenshots/sv-auth-01c-wrong-password.png" alt="TC-Auth-01 wrong password" width="700">

---

### TC-Sec-02: Role-based access
Each role gets only its own menu. The image shows the side menu of each of the five roles after login, from left to right: Receptionist, Doctor, Lab Technician, Pharmacist, Administrator. The refused doctor is under X-01 and TC-Sec-05.

<img src="../screenshots/sv-sec-02a-role-menus.png" alt="TC-Sec-02 menus of the five roles" width="700">


---

### TC-Sec-03: Password policy and hashing
Weak passwords are refused in the TC-Auth-01 screenshot. The stored values are salted Argon2id hashes, and two accounts with the same password have different values. Output of a read-only check, drawn as an image.

<img src="../screenshots/sv-sec-03a-salted-argon2-hashes.png" alt="TC-Sec-03 salted Argon2 hashes" width="700">

---

### TC-Sec-04: Account lockout
Locked on the fifth wrong password, the correct password refused while locked, then unlocked by the Admin. The account also unlocks by itself after the 15-minute cooldown (D-16); this run used the Admin unlock.

<img src="../screenshots/sv-sec-04b-locked-refuses-correct-password.png" alt="TC-Sec-04 correct password refused while locked" width="700">

<img src="../screenshots/sv-sec-04d-admin-unlock.png" alt="TC-Sec-04 Admin unlock" width="700">

---

### TC-Reg-01: Register a patient
A future birth date and a 5-digit mobile number are refused, then the corrected details are read back and the patient is saved.

<img src="../screenshots/sv-reg-01a-validation-errors.png" alt="TC-Reg-01 validation errors" width="700">

<img src="../screenshots/sv-reg-01c-saved.png" alt="TC-Reg-01 saved" width="700">

---

### TC-Reg-02: Unique UHID and duplicate-phone detection
A shared mobile number shows a SAME PHONE match, and "different person" issues a new UHID. Registering an existing patient again shows a STRONG match together with the SAME PHONE match.

<img src="../screenshots/sv-reg-02a-shared-phone-detected.png" alt="TC-Reg-02 shared phone detected" width="700">

<img src="../screenshots/sv-reg-02d-strong-match-shown.png" alt="TC-Reg-02 strong match" width="700">

---

### TC-Rec-02: Search
By part of a name, shared mobile number, UHID, name and date of birth, and a name with no match.

<img src="../screenshots/sv-rec-02b-search-by-shared-mobile.png" alt="TC-Rec-02 shared mobile" width="700">

<img src="../screenshots/sv-rec-02e-no-match.png" alt="TC-Rec-02 no match" width="700">

---

### TC-App-01: Book an appointment
Booked for today and for tomorrow. The slots offered start at the next quarter hour, so the first free slot of the day depends on the time of the run.

<img src="../screenshots/sv-app-01a-book-today.png" alt="TC-App-01 book today" width="700">

---

### TC-App-02: Prevent double-booking
The taken slot is not offered to the next patient, and the same patient at the same time with another doctor is refused.

<img src="../screenshots/sv-app-02a-taken-slot-not-offered.png" alt="TC-App-02 taken slot not offered" width="700">

<img src="../screenshots/sv-app-02b-patient-clash-refused.png" alt="TC-App-02 patient clash refused" width="700">

---

### TC-Doc-03: Record a consultation
Refused before the start (on the first screen, before any form), saved after the start with a medicine and a test, and refused a second time. A second consultation (CON-000002) was saved for APT-000001 later in the run.

<img src="../screenshots/sv-doc-03a-before-start-refused.png" alt="TC-Doc-03 before start refused" width="700">

<img src="../screenshots/sv-doc-03b-consultation-recorded.png" alt="TC-Doc-03 consultation recorded" width="700">

<img src="../screenshots/sv-doc-03c-second-consultation-refused.png" alt="TC-Doc-03 second consultation refused" width="700">

---

### TC-Bill-01: Itemized bill
An unfinished visit is refused. A completed visit is billed with the consultation fee, a test price and a service charge.

<img src="../screenshots/sv-bill-01a-unfinished-visit-refused.png" alt="TC-Bill-01 unfinished visit refused" width="700">

<img src="../screenshots/sv-bill-01b-itemized-bill.png" alt="TC-Bill-01 itemized bill" width="700">

---

### TC-Bill-02: Payment
A part payment, an overpayment refused, and the final payment.

<img src="../screenshots/sv-bill-02b-overpayment-refused.png" alt="TC-Bill-02 overpayment refused" width="700">

<img src="../screenshots/sv-bill-02c-final-payment.png" alt="TC-Bill-02 final payment" width="700">

---

### TC-Bill-03: Receipt
A receipt printed again.

<img src="../screenshots/sv-bill-03a-receipt-printed-again.png" alt="TC-Bill-03 receipt printed again" width="700">

---

### TC-Lab-01: Lab results
The waiting test, the first result, the corrected result, then the result as seen by the ordering doctor and by another doctor.

<img src="../screenshots/sv-lab-01d-ordering-doctor-sees-result.png" alt="TC-Lab-01 ordering doctor sees result" width="700">

<img src="../screenshots/sv-lab-01e-other-doctor-sees-nothing.png" alt="TC-Lab-01 other doctor sees nothing" width="700">

---

### TC-Pharm-01: Dispensing and stock
Stock with two LOW STOCK alerts; the warning when the medicine picked is not the one prescribed (the box that allows it is unchecked, and Dispense is disabled); the correct dispense, with a low-stock alert; more than the prescribed quantity refused; a full dispense; and a restock.

<img src="../screenshots/sv-pharm-01b-wrong-medicine-warning.png" alt="TC-Pharm-01 wrong medicine warning" width="700">

<img src="../screenshots/sv-pharm-01c-dispensed-and-low-stock-alert.png" alt="TC-Pharm-01 dispensed with low stock alert" width="700">

<img src="../screenshots/sv-pharm-01f-restocked.png" alt="TC-Pharm-01 restocked" width="700">

---

### TC-Perf-01: Response time
`hms_seed --store perf.db --count 100000 --bench`, 100 runs per operation on a Debug build with an encrypted store. Every operation is far inside the 3 second limit; the tool reports the 90th percentile. The timings cover the service calls, not the drawing of the screen.

<img src="../screenshots/sv-perf-01a-timings.png" alt="TC-Perf-01 timings" width="700">

---

### TC-Sec-01: No plaintext PII or medical data
Checked after the full run, on the store the whole run used. The file has no recognisable header and none of the searched strings (patient names, mobile numbers, addresses, clinical text, user names and table names) is readable in it. The audit entries hold IDs and field names only (0 matches for patient text in the audit table).

<img src="../screenshots/sv-sec-01a-no-readable-text-in-store.png" alt="TC-Sec-01 no readable text in the store" width="700">

---

### TC-Rec-01: Medical history versions
The first version is saved, then only the allergies change. The earlier version stays as it was.

<img src="../screenshots/sv-rec-01b-second-version-earlier-listed.png" alt="TC-Rec-01 second version" width="700">

---

### TC-App-03: Reschedule, cancel and cutoff
Reschedule and cancel work outside the cutoff, and the slot is released. A cancel inside the 60-minute cutoff is refused.

<img src="../screenshots/sv-app-03a-reschedule.png" alt="TC-App-03 reschedule" width="700">

<img src="../screenshots/sv-app-03c-cancel-inside-cutoff-refused.png" alt="TC-App-03 cancel inside cutoff refused" width="700">

---

### TC-App-04: Reminders
NEW on the first listing and "listed before" on the second.

<img src="../screenshots/sv-app-04a-reminders-new.png" alt="TC-App-04 reminders new" width="700">

<img src="../screenshots/sv-app-04b-reminders-listed-before.png" alt="TC-App-04 reminders listed before" width="700">

---

### TC-Doc-01: Doctor profile
Department and weekly hours of Dr mehta, as set by the Admin. The doctor is offered on the booking screen in TC-App-01.

<img src="../screenshots/sv-doc-01a-profile-and-weekly-hours.png" alt="TC-Doc-01 profile and hours" width="700">

---

### TC-Doc-02: OPD queue
The doctor's own appointments for today.

<img src="../screenshots/sv-doc-02a-doctor-queue.png" alt="TC-Doc-02 doctor queue" width="700">

---

### TC-Staff-01: Manage staff accounts
The accounts, a deactivated account, the Admin refused when deactivating their own account, the deactivated account refused at login, then reactivation with a new password and a login with it.

<img src="../screenshots/sv-staff-01c-self-deactivation-refused.png" alt="TC-Staff-01 self deactivation refused" width="700">

<img src="../screenshots/sv-staff-01d-deactivated-login-refused.png" alt="TC-Staff-01 deactivated login refused" width="700">

<img src="../screenshots/sv-staff-01f-login-with-new-password.png" alt="TC-Staff-01 login with the new password" width="700">

---

### TC-Admin-01: Master data
A department and a room added. The departments of the two doctors appear in the department list, each with one doctor, without the Admin typing them in again (D-59). The mapping lists each doctor's department.

<img src="../screenshots/sv-admin-01a-department-added.png" alt="TC-Admin-01 department added" width="700">

<img src="../screenshots/sv-admin-01b-room-added.png" alt="TC-Admin-01 room added" width="700">

---

### TC-Admin-02: System settings
The cutoff changed from 60 to 45 minutes, and a cancel 59 minutes before the start that the 60-minute rule would have refused, accepted after the change (APT-000006 was booked for 20:30 and cancelled at 19:30:50). The three parameters are listed in the first image.

<img src="../screenshots/sv-admin-02b-cutoff-changed.png" alt="TC-Admin-02 cutoff changed" width="700">

<img src="../screenshots/sv-admin-02d-cancel-allowed-under-new-cutoff.png" alt="TC-Admin-02 cancel allowed under the new cutoff" width="700">

---

### TC-Rep-01: Summary report
The figures match the data entered in this run (APT-000005 and APT-000006, booked and cancelled for TC-Admin-02 after the report was taken, are not in it).

<img src="../screenshots/sv-rep-01a-summary-report.png" alt="TC-Rep-01 summary report" width="700">

---

### TC-Sec-05: Audit trail
The newest entries, then an older part of the log with the lockout, the denied logins and their reason codes, the unlock, and the account and fee changes. The last two columns are cut off by the window width.

<img src="../screenshots/sv-sec-05b-audit-lockout-and-denials.png" alt="TC-Sec-05 lockout and denials" width="700">

---

### TC-Sec-06: Data-store access
`icacls` on a store created by the window program: the folder, the store file and the lock file each list only the running user. Output of the command, drawn as an image.

<img src="../screenshots/sv-sec-06a-file-permissions.png" alt="TC-Sec-06 file permissions" width="700">

---

### X-01: Visit history
A doctor with no appointment is refused both the medical history and the visit history. The treating doctor sees the consultation in full.

<img src="../screenshots/sv-x01a-other-doctor-history-refused.png" alt="X-01 other doctor medical history refused" width="700">

<img src="../screenshots/sv-x01c-treating-doctor-visit-history.png" alt="X-01 treating doctor visit history" width="700">

---

### X-02: Start-up
Declining the new-store question creates nothing. A damaged store is refused after three wrong passphrases and its SHA-256 is the same before and after.

<img src="../screenshots/sv-x02a-new-store-needs-confirmation.png" alt="X-02 new store needs confirmation" width="700">

<img src="../screenshots/sv-x02b-damaged-store-refused.png" alt="X-02 damaged store refused" width="700">

<img src="../screenshots/sv-x02c-damaged-store-unchanged-sha256.png" alt="X-02 damaged store unchanged" width="700">

---

### X-03: Single instance
The second instance is refused while the first keeps running. After the first is closed, the next start reaches the passphrase screen.

<img src="../screenshots/sv-x03a-second-instance-refused.png" alt="X-03 second instance refused" width="700">

---

### X-04: Correct a patient's details
The change is read back before it is saved, then saved. The audit entry names the field (`address`) and not the value.

<img src="../screenshots/sv-x04b-edit-patient-saved.png" alt="X-04 saved" width="700">

## Defects found in this run

Both were in the window program, were fixed in the same change, and were checked again afterwards. Neither is in the console program.

- **A refused booking showed no message.** When the chosen slot or the patient's time was already taken, the booking was refused correctly and nothing was saved, but the screen refreshed itself and erased the refusal, so the receptionist saw no explanation. The refusal now stays on screen (the image under TC-App-02 is from after the fix). There is no automated test for the message itself.
- **Lab Technician and Pharmacist could not select a patient.** The "Tests of a patient" and "Prescriptions of a patient" screens looked the patient up as a patient record, which those roles may not view, so the lookup was refused and written to the audit log as a denied `PATIENT_VIEW`. The console menus only check the UHID, as those screens now do. A test now fails if either screen opens the record (`tests/test_gui.cpp`).

## Observations that are not defects

- The window program shows a receipt on screen and cannot save it as a text file, which the console program can (D-46, D-61).
- Text in scripts the font does not contain, for example Devanagari, would show as "?" (D-61).
- The last two columns of the audit log table are cut off at the default window width.
- The reminder screen says "Contact the patients marked NEW" also when 0 new reminders were recorded.
- Timings were taken on a Debug build and exclude drawing the screen.

## Known limitations

Known limitations and agreed differences from the SRS, SAD and Test Plan are recorded in [`docs/deviation-log.md`](../deviation-log.md).
