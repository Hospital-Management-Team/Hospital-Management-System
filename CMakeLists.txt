cmake_minimum_required(VERSION 3.16)
project(HospitalManagementSystem VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(HMS_BUILD_TESTS "Build the doctest unit and integration tests" ON)
option(HMS_WERROR "Treat compiler warnings in HMS code as errors" OFF)
option(HMS_SANITIZE "Build with AddressSanitizer (+UBSan on GCC/Clang)" OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

if(HMS_SANITIZE)
  if(MSVC)
    add_compile_options(/fsanitize=address)
  else()
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address,undefined)
  endif()
endif()

# ---- SQLite (third_party/sqlite) -------------------------------------------
add_library(hms_sqlite STATIC third_party/sqlite/sqlite3.c)
target_include_directories(hms_sqlite SYSTEM PUBLIC third_party/sqlite)
target_compile_definitions(hms_sqlite PRIVATE
  SQLITE_DQS=0
  SQLITE_DEFAULT_FOREIGN_KEYS=1
  SQLITE_OMIT_DEPRECATED
  SQLITE_THREADSAFE=1)
if(MSVC)
  target_compile_options(hms_sqlite PRIVATE /W0)
else()
  target_compile_options(hms_sqlite PRIVATE -w)
  find_package(Threads REQUIRED)
  target_link_libraries(hms_sqlite PUBLIC Threads::Threads ${CMAKE_DL_LIBS})
endif()

# ---- Argon2 (third_party/argon2, reference implementation) -----------------
add_library(hms_argon2 STATIC
  third_party/argon2/src/argon2.c
  third_party/argon2/src/core.c
  third_party/argon2/src/encoding.c
  third_party/argon2/src/ref.c
  third_party/argon2/src/thread.c
  third_party/argon2/src/blake2/blake2b.c)
target_include_directories(hms_argon2 SYSTEM PUBLIC third_party/argon2/include)
target_include_directories(hms_argon2 PRIVATE third_party/argon2/src)
if(MSVC)
  target_compile_options(hms_argon2 PRIVATE /W0)
else()
  target_compile_options(hms_argon2 PRIVATE -w)
  find_package(Threads REQUIRED)
  target_link_libraries(hms_argon2 PUBLIC Threads::Threads)
endif()

# ---- Warning flags for our own code ----------------------------------------
add_library(hms_warnings INTERFACE)
if(MSVC)
  target_compile_options(hms_warnings INTERFACE /W4 /permissive- /utf-8 /EHsc)
  target_compile_definitions(hms_warnings INTERFACE _CRT_SECURE_NO_WARNINGS NOMINMAX WIN32_LEAN_AND_MEAN)
  if(HMS_WERROR)
    target_compile_options(hms_warnings INTERFACE /WX)
  endif()
else()
  target_compile_options(hms_warnings INTERFACE -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wno-sign-conversion)
  if(HMS_WERROR)
    target_compile_options(hms_warnings INTERFACE -Werror)
  endif()
endif()

# ---- Sources -----------------------------------------------------------------
set(HMS_FOUNDATION_SOURCES
  src/core/result.cpp
  src/core/clock.cpp
  src/core/text.cpp
  src/core/ids.cpp
  src/data/database.cpp
  src/data/schema.cpp
  src/data/store.cpp
  src/platform/store_lock.cpp
  src/platform/console_setup.cpp
  src/validation/input.cpp
  src/ui/console_io.cpp
)

set(HMS_FEATURE_SOURCES
  # Patient registration and search (issue #2)
  src/audit/audit_log.cpp
  src/auth/authorizer.cpp
  src/patient/uhid.cpp
  src/patient/phone.cpp
  src/patient/patient.cpp
  src/patient/patient_validation.cpp
  src/patient/patient_repository.cpp
  src/patient/patient_service.cpp
  src/ui/console_app.cpp
  # Staff authentication (issue #3)
  src/auth/authentication.cpp
  # Appointments (issue #4)
  src/appointment/appointment.cpp
  src/appointment/appointment_repository.cpp
  src/appointment/appointment_service.cpp
  src/ui/appointment_screens.cpp
  src/ui/doctor_schedule_screen.cpp
  src/ui/doctor_workspace_screens.cpp
  # Consultations (issue #5)
  src/consultation/consultation_repository.cpp
  src/consultation/consultation_service.cpp
  # Billing and payments (issue #7)
  src/core/money.cpp
  src/billing/billing.cpp
  src/billing/billing_repository.cpp
  src/billing/billing_service.cpp
  src/billing/receipt.cpp
  src/ui/billing_screens.cpp
)

add_library(hms_core STATIC ${HMS_FOUNDATION_SOURCES} ${HMS_FEATURE_SOURCES})
target_include_directories(hms_core PUBLIC src)
target_link_libraries(hms_core PUBLIC hms_sqlite hms_argon2 PRIVATE hms_warnings)
if(WIN32)
  target_link_libraries(hms_core PUBLIC bcrypt)
endif()

add_executable(hms src/app/main.cpp)
target_link_libraries(hms PRIVATE hms_core hms_warnings)

# ---- Developer tools ---------------------------------------------------------
add_executable(hms_seed tools/hms_seed.cpp)
target_link_libraries(hms_seed PRIVATE hms_core hms_warnings)

# ---- Tests -------------------------------------------------------------------
if(HMS_BUILD_TESTS)
  enable_testing()
  set(HMS_TEST_SOURCES
    tests/test_main.cpp
    tests/test_core.cpp
    tests/test_input.cpp
    tests/test_console_io.cpp
    tests/test_store.cpp
  )
  set(HMS_FEATURE_TEST_SOURCES
    # issue #2
    tests/test_uhid.cpp
    tests/test_phone.cpp
    tests/test_patient_validation.cpp
    tests/test_registration.cpp
    tests/test_search.cpp
    tests/test_patient_store.cpp
    tests/test_console_app.cpp
    # issue #3
    tests/test_authentication.cpp
    # issue #4
    tests/test_appointment.cpp
    tests/test_appointment_console.cpp
    # issue #5
    tests/test_consultation.cpp
    tests/test_doctor_workspace_console.cpp
    # issue #7
    tests/test_billing.cpp
    tests/test_billing_console.cpp
  )
  add_executable(hms_tests ${HMS_TEST_SOURCES} ${HMS_FEATURE_TEST_SOURCES})
  target_include_directories(hms_tests SYSTEM PRIVATE third_party/doctest)
  target_include_directories(hms_tests PRIVATE tests)
  target_link_libraries(hms_tests PRIVATE hms_core hms_warnings)
  add_test(NAME hms_tests COMMAND hms_tests)
endif()