cmake_minimum_required(VERSION 3.20)

project(HospitalManagementSystem LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

find_package(PkgConfig REQUIRED)
pkg_check_modules(SQLITE3 REQUIRED IMPORTED_TARGET sqlite3)

add_executable(hms src/main.cpp)
target_link_libraries(hms PRIVATE PkgConfig::SQLITE3)
