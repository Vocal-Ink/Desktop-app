# Third-party dependencies, fetched from their upstream git repositories and
# built as static libraries so the app ships as a single executable (plus Qt).
include(FetchContent)

set(FETCHCONTENT_QUIET ON)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "Build shared libraries" FORCE)

set(_vi_fetch_opts GIT_SHALLOW TRUE GIT_PROGRESS FALSE)
if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.28)
    # Keep dependency targets out of `all` and out of `cmake --install`.
    list(APPEND _vi_fetch_opts EXCLUDE_FROM_ALL)
endif()

# --- whisper.cpp: on-device speech recognition (MIT) -------------------------
if(VOCALINK_WITH_WHISPER)
    set(WHISPER_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
    set(WHISPER_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(WHISPER_BUILD_SERVER   OFF CACHE BOOL "" FORCE)
    set(WHISPER_ALL_WARNINGS   OFF CACHE BOOL "" FORCE)
    set(WHISPER_CURL           OFF CACHE BOOL "" FORCE)
    set(WHISPER_SDL2           OFF CACHE BOOL "" FORCE)
    # Portable CPU build (AVX2/FMA baseline on x86-64) rather than -march=native,
    # so release binaries run on other machines.
    set(GGML_NATIVE  OFF CACHE BOOL "" FORCE)
    # ggml's own thread pool avoids shipping an OpenMP runtime.
    set(GGML_OPENMP  OFF CACHE BOOL "" FORCE)
    set(GGML_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
    set(GGML_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    if(APPLE)
        set(GGML_METAL_EMBED_LIBRARY ON CACHE BOOL "" FORCE)
    endif()

    FetchContent_Declare(whisper
        GIT_REPOSITORY https://github.com/ggml-org/whisper.cpp.git
        GIT_TAG        v1.9.4
        ${_vi_fetch_opts})
    FetchContent_MakeAvailable(whisper)
endif()

# --- QHotkey: system-wide shortcuts (BSD-3-Clause) ----------------------------
if(VOCALINK_WITH_HOTKEYS)
    set(QT_DEFAULT_MAJOR_VERSION 6 CACHE STRING "" FORCE)
    set(QHOTKEY_INSTALL  OFF CACHE BOOL "" FORCE)
    set(QHOTKEY_EXAMPLES OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(qhotkey
        GIT_REPOSITORY https://github.com/Skycoder42/QHotkey.git
        GIT_TAG        1.5.0
        ${_vi_fetch_opts})
    # QHotkey declares cmake_minimum_required(3.1), which CMake 4 refuses.
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
    FetchContent_MakeAvailable(qhotkey)
    unset(CMAKE_POLICY_VERSION_MINIMUM)
endif()

# --- QtKeychain: API keys in the OS credential store (BSD-3-Clause) ----------
if(VOCALINK_WITH_KEYCHAIN)
    set(BUILD_TRANSLATIONS     OFF CACHE BOOL "" FORCE)
    set(BUILD_TEST_APPLICATION OFF CACHE BOOL "" FORCE)
    set(BUILD_WITH_QT5         OFF CACHE BOOL "" FORCE)
    # qtkeychain calls include(CTest); keep its autotests out of our test run.
    set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(qtkeychain
        GIT_REPOSITORY https://github.com/frankosterfeld/qtkeychain.git
        GIT_TAG        0.17.0
        ${_vi_fetch_opts})
    FetchContent_MakeAvailable(qtkeychain)
    # Static build: consumers must not see dllimport decorations.
    target_compile_definitions(qt6keychain PUBLIC QT6KEYCHAIN_STATIC_DEFINE)
endif()
