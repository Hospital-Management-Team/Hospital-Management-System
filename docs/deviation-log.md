# Deviation log

The Part-2 brief (item 2i) says: "Deviations from the submitted document should be documented and explained during the final demo." This log lists every way the build differs from the **submitted** documents: SRS v1.1, SAD v1.0 and Software Test Plan v1.0.

Each pull request that introduces a difference adds a row. The SRS v11 working draft referred to in code comments is the team's revised SRS, which supplies the detailed rules that SRS v1.1 leaves open.

**Status:**
- **Temporary**: closed by a later issue.
- **Permanent**: a deliberate decision, explained here.
- **Open**: still needs a team decision.

| ID | Submitted document says | The build does | Why | Status | Added by |
|---|---|---|---|---|---|
| D-05 | SRS 2.4 / SAD ADR-002: "file-based or lightweight relational database". | SQLite 3.50.4, vendored as source in `third_party/`. | It is within what was submitted; this records the choice. Vendoring makes the Windows and Linux builds identical. | Permanent (decision) | #1 |
| D-06 | HMS-NF-003, HMS-SR-005: sensitive data encrypted at rest. | Not encrypted. On Linux the store and lock file are created owner-only (mode 0600). | Encrypting the store needs an extra library (for example SQLCipher). Field encryption would break name and phone search. Disk encryption (BitLocker, LUKS) is a different protection and is not claimed to meet the requirement. | Open | #1 |
| D-07 | HMS-SR-005: restrict file access to the application. | On Windows the store and lock file are created with default permissions. | Windows owner-only ACLs are not implemented yet. | Temporary (#12) | #1 |
| D-08 | HMS-NF-002: ≥ 99 % availability, measured from uptime logs. | Crash safety instead: changes are all-or-nothing transactions, and failure-injection tests show nothing is half-saved. | Uptime can't be measured for a single-user desktop program, and no uptime log exists. | Permanent | #1 |
| D-10 | STP 6: Unity/CppUTest/Google Test, Valgrind, Jira/Trello. | doctest, AddressSanitizer + UBSan, GitHub Issues and GitHub Actions. | doctest is one header and builds identically on MSVC and GCC. Valgrind doesn't exist on Windows. The course uses GitHub. | Permanent | #1 |
| D-13 | — | An older data store is upgraded to the current schema on opening, one numbered migration at a time; a newer one is refused unchanged. | Each feature adds tables. | Permanent | #1 |
