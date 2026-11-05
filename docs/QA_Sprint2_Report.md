cat > docs/QA\_Sprint2\_Report.md <<'EOF'

\# Sprint 2 – QA Verification Report



\## 1. Project Details



\- Project: Hospital Management System

\- Sprint: Sprint 2

\- QA Owner: Harsh Raj

\- Role: QA Lead



\## 2. Build Verification



The application was built successfully using CMake.



| Test | Result |

|---|---|

| CMake build | Passed |

| CTest execution | 1/1 test passed |

| Application test suite | 24/24 test cases passed |

| Total assertions | 2117/2117 passed |



\## 3. Functional Testing



The following application functionalities were verified:



| Functionality | Result |

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



\## 4. Security Verification



The security-related functionality was verified:



\- Staff authentication using username and password.

\- Role-based access for different staff categories.

\- Administrator account management.

\- Password complexity validation.

\- Staff account management.

\- Audit log functionality.



\## 5. Non-Functional Requirement Verification



\- Application build and execution were verified.

\- Patient database containing 100,000 records was used for test-data verification.

\- Database integrity and table availability were checked.

\- Application functionality was evaluated against the defined performance, security, and usability requirements.



\## 6. Conclusion



Sprint 2 QA verification was completed successfully.



The application build and automated test suite completed successfully. Functional workflows, staff authentication, patient management, appointment-related functionality, and security features were included in the verification process.



\*\*Overall QA Status: PASSED\*\*



EOF

