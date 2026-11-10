cmake_minimum_required(VERSION 3.16)
project(HospitalManagementSystem VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(HMS_BUILD_TESTS "Build the doctest unit and integration tests" ON)
option(HMS_BUILD_GUI "Build the graphical front end (hms_gui: Dear ImGui + GLFW + OpenGL)" ON)
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

# ---- SQLite3 Multiple Ciphers (third_party/sqlite3mc) ---------------------
add_library(hms_sqlite STATIC third_party/sqlite3mc/sqlite3mc_amalgamation.c)
target_include_directories(hms_sqlite SYSTEM PUBLIC third_party/sqlite3mc)
target_compile_definitions(hms_sqlite PRIVATE
  SQLITE_DQS=0
  SQLITE_DEFAULT_FOREIGN_KEYS=1
  SQLITE_OMIT_DEPRECATED
  SQLITE_THREADSAFE=1
  # Only ChaCha20-Poly1305 (authenticated) is built in; the store is always encrypted with it.
  HAVE_CIPHER_AES=0
  HAVE_CIPHER_RC4=0
  HAVE_CIPHER_SQLCIPHER=0
  HAVE_CIPHER_ASCON128=0
  HAVE_CIPHER_AEGIS=0)
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
  src/config/settings.cpp
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
  # Medical records (issue #6)
  src/record/medical_record_repository.cpp
  src/record/medical_record_service.cpp
  src/ui/medical_record_screens.cpp
  src/audit/audit_log_service.cpp
  src/admin/admin_repository.cpp
  src/admin/admin_service.cpp
  src/ui/admin_screens.cpp
  src/ui/audit_log_screen.cpp
  # Laboratory and pharmacy (issue #8)
  src/lab/lab_repository.cpp
  src/lab/lab_service.cpp
  src/pharmacy/pharmacy_repository.cpp
  src/pharmacy/pharmacy_service.cpp
  src/ui/lab_pharmacy_screens.cpp
)

add_library(hms_core STATIC ${HMS_FOUNDATION_SOURCES} ${HMS_FEATURE_SOURCES})
target_include_directories(hms_core PUBLIC src)
target_link_libraries(hms_core PUBLIC hms_sqlite hms_argon2 PRIVATE hms_warnings)
if(WIN32)
  target_link_libraries(hms_core PUBLIC bcrypt advapi32)
endif()

add_executable(hms src/app/main.cpp)
target_link_libraries(hms PRIVATE hms_core hms_warnings)

# ---- Graphical front end (src/gui): Dear ImGui + GLFW + OpenGL ----------------
# The GUI is another front end for the same services as the console application.
# Turn it off with -DHMS_BUILD_GUI=OFF on a machine without the OpenGL/X11 headers.
if(HMS_BUILD_GUI)
  set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
  set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "" FORCE)
  add_subdirectory(third_party/glfw)
  find_package(OpenGL REQUIRED)

  add_library(hms_imgui STATIC
    third_party/imgui/imgui.cpp
    third_party/imgui/imgui_draw.cpp
    third_party/imgui/imgui_tables.cpp
    third_party/imgui/imgui_widgets.cpp
    third_party/imgui/misc/imgui_stdlib.cpp)
  target_include_directories(hms_imgui SYSTEM PUBLIC third_party/imgui third_party/imgui/misc)
  if(MSVC)
    target_compile_options(hms_imgui PRIVATE /W0)
  else()
    target_compile_options(hms_imgui PRIVATE -w)
  endif()

  add_library(hms_imgui_backends STATIC
    third_party/imgui/backends/imgui_impl_glfw.cpp
    third_party/imgui/backends/imgui_impl_opengl3.cpp)
  target_include_directories(hms_imgui_backends SYSTEM PUBLIC third_party/imgui/backends)
  target_link_libraries(hms_imgui_backends PUBLIC hms_imgui glfw OpenGL::GL)
  if(MSVC)
    target_compile_options(hms_imgui_backends PRIVATE /W0)
  else()
    target_compile_options(hms_imgui_backends PRIVATE -w)
  endif()

  # Screens and the application shell. No window or OpenGL code, so the tests can run it.
  set(HMS_GUI_SOURCES
    src/gui/ui.cpp
    src/gui/context.cpp
    src/gui/patient_picker.cpp
    src/gui/screens_patients.cpp
    src/gui/screens_appointments.cpp
    src/gui/screens_billing.cpp
    src/gui/screens_doctor.cpp
    src/gui/screens_lab_pharmacy.cpp
    src/gui/screens_admin.cpp
    src/gui/gui_app.cpp
  )
  add_library(hms_gui_core STATIC ${HMS_GUI_SOURCES})
  target_include_directories(hms_gui_core PUBLIC src)
  target_link_libraries(hms_gui_core PUBLIC hms_core hms_imgui PRIVATE hms_warnings)

  add_executable(hms_gui src/gui/main_gui.cpp)
  target_link_libraries(hms_gui PRIVATE hms_gui_core hms_imgui_backends hms_warnings)
  if(MSVC)
    # A window application (no console window) that still starts at main().
    target_link_options(hms_gui PRIVATE /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup)
  endif()
endif()

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
    # HMS-F-005 (edit patient demographics)
    tests/test_patient_update.cpp
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
    # issue #6
    tests/test_medical_record.cpp
    tests/test_medical_record_console.cpp
    tests/test_audit_log_view.cpp
    tests/test_admin.cpp
    tests/test_settings.cpp
    # issue #8
    tests/test_lab.cpp
    tests/test_pharmacy.cpp
    tests/test_lab_pharmacy_console.cpp
  )
  add_executable(hms_tests ${HMS_TEST_SOURCES} ${HMS_FEATURE_TEST_SOURCES})
  target_include_directories(hms_tests SYSTEM PRIVATE third_party/doctest)
  target_include_directories(hms_tests PRIVATE tests)
  target_link_libraries(hms_tests PRIVATE hms_core hms_warnings)
  if(HMS_BUILD_GUI)
    target_sources(hms_tests PRIVATE tests/test_gui.cpp)
    target_link_libraries(hms_tests PRIVATE hms_gui_core)
  endif()
  add_test(NAME hms_tests COMMAND hms_tests)
endif()