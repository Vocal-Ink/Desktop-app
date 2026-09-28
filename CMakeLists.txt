cmake_minimum_required(VERSION 3.25)

project(VocalInk
    VERSION 0.1.0
    DESCRIPTION "Speak-for-me assistive text-to-speech for people who can't (or don't want to) use their voice"
    HOMEPAGE_URL "https://github.com/Vocal-Ink/Desktop-app"
    LANGUAGES C CXX)

set(VOCALINK_DISPLAY_NAME "Vocal Ink")
set(VOCALINK_ORG_NAME "Vocal-Ink")
set(VOCALINK_BUNDLE_ID "org.vocalink.desktop")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Vocal Ink's own virtual mic drivers (docs/VIRTUAL_AUDIO.md). The Windows driver builds
# with MSBuild from driver/windows; the macOS HAL plug-in builds here.
option(VOCALINK_WITH_MAC_DRIVER "Build the macOS virtual mic HAL plug-in (VocalInkVirtualMic.driver)" OFF)
option(VOCALINK_DRIVER_ONLY     "Configure only the virtual mic driver: no Qt, app or tests" OFF)

if(VOCALINK_DRIVER_ONLY)
    if(NOT (APPLE AND VOCALINK_WITH_MAC_DRIVER))
        message(FATAL_ERROR "VOCALINK_DRIVER_ONLY builds the macOS driver: use it on macOS with "
                            "-DVOCALINK_WITH_MAC_DRIVER=ON. The Windows driver builds with MSBuild "
                            "(driver/windows/VocalInkAudio.sln).")
    endif()
    add_subdirectory(driver/macos)
    return()
endif()

option(VOCALINK_WITH_WHISPER  "Local speech recognition with whisper.cpp" ON)
option(VOCALINK_WITH_HOTKEYS  "System-wide hotkeys with QHotkey" ON)
option(VOCALINK_WITH_KEYCHAIN "Store API keys in the OS keychain with QtKeychain" ON)
option(VOCALINK_BUILD_TESTS   "Build the unit tests" ON)

set(VOCALINK_QT_COMPONENTS Core Gui Widgets Network Multimedia TextToSpeech WebSockets Concurrent)
if(VOCALINK_BUILD_TESTS)
    list(APPEND VOCALINK_QT_COMPONENTS Test)
endif()
find_package(Qt6 6.4 REQUIRED COMPONENTS ${VOCALINK_QT_COMPONENTS})
message(STATUS "Vocal Ink: building against Qt ${Qt6_VERSION}")

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)

include(cmake/Dependencies.cmake)

add_subdirectory(src)

if(APPLE AND VOCALINK_WITH_MAC_DRIVER)
    add_subdirectory(driver/macos)
endif()

if(VOCALINK_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

include(cmake/Install.cmake)
