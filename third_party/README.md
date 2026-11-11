# Third-party code

The libraries are vendored (copied into the repository) so the project builds on Windows and Linux with nothing to install except a C++17 compiler and CMake.

| Library | Version | Files | Licence | Used for |
|---|---|---|---|---|
| SQLite3 Multiple Ciphers | 2.6.1, built on SQLite 3.54.0 (amalgamation) | `sqlite3mc/sqlite3mc_amalgamation.c`, `sqlite3mc/sqlite3mc_amalgamation.h` | MIT (SQLite itself is public domain) | The encrypted data store (SAD ADR-002, HMS-NF-003) |
| doctest | 2.4.11 | `doctest/doctest.h` | MIT | Unit and integration tests |
| Dear ImGui | 1.91.9 | `imgui/` (core files, `backends/imgui_impl_glfw.*`, `backends/imgui_impl_opengl3.*` with its loader, `misc/imgui_stdlib.*`, `LICENSE.txt`) | MIT | The window program `hms_gui` (drawing the screens) |
| GLFW | 3.4 | `glfw/` (`CMakeLists.txt`, `CMake/`, `include/`, `src/`, `LICENSE.md`) | zlib/libpng | The window program `hms_gui` (window, keyboard and mouse, OpenGL context) |

SHA-256 of the vendored files:

```
9b129d1ceb2c7d5dbf374b833eb29a640ecfa8e4a5af6d48508b429019c707b0  sqlite3mc/sqlite3mc_amalgamation.c
99b3e6997aa5fc604d9ea2352e6ae89dfd4d0af8d5bd7cbfe7293d959779e6eb  sqlite3mc/sqlite3mc_amalgamation.h
44faa038e9c3f9728efbda143748d01124ea0a27f4bf78f35a15d8fab2e039fb  doctest/doctest.h
```

The SQLite3 Multiple Ciphers amalgamation was taken unmodified from https://github.com/utelle/SQLite3MultipleCiphers (release v2.6.1, file `sqlite3mc-2.6.1-sqlite-3.54.0-amalgamation.zip`, SHA-256 `3f25af603566a23c821d8e581778b5ece2a541d2435776de57b8d845c176815a`, matching the release's published SHA256SUMS). It is a drop-in replacement for SQLite that adds encryption. The build contains only the ChaCha20-Poly1305 cipher (the other ciphers are switched off in `CMakeLists.txt`), and the application always uses it. To update it, download the newer `...-amalgamation.zip` from the release page, verify it against the published SHA256SUMS, and copy `sqlite3mc_amalgamation.c` and `sqlite3mc_amalgamation.h` here. doctest came from https://github.com/doctest/doctest (tag v2.4.11).

Dear ImGui came from https://github.com/ocornut/imgui (tag v1.91.9) and GLFW from https://github.com/glfw/glfw (tag 3.4). Only the files the build uses are kept: for ImGui the core, the GLFW and OpenGL 3 backends and `imgui_stdlib`; for GLFW the sources, headers and CMake files, without its documentation, examples, tests and other build systems. The window program is optional: `-DHMS_BUILD_GUI=OFF` leaves both libraries out of the build. GLFW's own CMake build is used through `add_subdirectory(third_party/glfw)`; to update either library, replace its folder with the newer tag and run the tests.
