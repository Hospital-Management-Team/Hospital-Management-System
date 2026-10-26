cmake_minimum_required(VERSION 3.16)
project(HospitalManagementSystem VERSION 0.1.0 LANGUAGES C CXX)

# ---------------------------------------------------------------------------
# Hospital Management System (Team 2)
# C++17, SQLite (vendored amalgamation), doctest (vendored single header).
# Builds on Windows (MSVC) and Linux (GCC/Clang) with nothing to install
# except a compiler and CMake.
# ---------------------------------------------------------------------------

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

# ---- SQLite (third_party/sqlite, amalgamation 3.50.4) ----------------------
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
# Foundation (issue #1) and input layer (issue #13).
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

# Feature modules: each feature pull request adds its sources here.
set(HMS_FEATURE_SOURCES
  src/auth/authentication.cpp
)

add_library(hms_core STATIC ${HMS_FOUNDATION_SOURCES} ${HMS_FEATURE_SOURCES})
target_include_directories(hms_core PUBLIC src)
target_include_directories(hms_core PRIVATE
  "C:/Users/jithu/OneDrive/Pictures/Desktop/New folder/ucrt64/include"
)

target_link_libraries(hms_core PUBLIC
  "C:/Users/jithu/OneDrive/Pictures/Desktop/New folder/ucrt64/lib/libargon2.a"
)
target_link_libraries(hms_core PUBLIC hms_sqlite PRIVATE hms_warnings)
if(WIN32)
    target_link_libraries(hms_core PUBLIC bcrypt)
endif()

add_executable(hms src/app/main.cpp)
target_link_libraries(hms PRIVATE hms_core hms_warnings)

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
  # Feature pull requests add their test files here.
  set(HMS_FEATURE_TEST_SOURCES
  )
  add_executable(hms_tests ${HMS_TEST_SOURCES} ${HMS_FEATURE_TEST_SOURCES})
  target_include_directories(hms_tests SYSTEM PRIVATE third_party/doctest)
  target_include_directories(hms_tests PRIVATE tests)
  target_link_libraries(hms_tests PRIVATE hms_core hms_warnings)
  add_test(NAME hms_tests COMMAND hms_tests)
endif()
