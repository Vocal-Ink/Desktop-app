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

option(VOCALINK_WITH_WHISPER  "Local speech recognition with whisper.cpp" ON)
option(VOCALINK_WITH_HOTKEYS  "System-wide hotkeys with QHotkey" ON)
option(VOCALINK_WITH_KEYCHAIN "Store API keys in the OS keychain with QtKeychain" ON)
option(VOCALINK_BUILD_TESTS   "Build the unit tests" ON)

set(VOCALINK_QT_COMPONENTS Core Gui Widgets Network Multimedia TextToSpeech WebSockets Concurrent
    Qml Quick QuickControls2)
if(VOCALINK_BUILD_TESTS)
    list(APPEND VOCALINK_QT_COMPONENTS Test)
endif()
find_package(Qt6 6.4 REQUIRED COMPONENTS ${VOCALINK_QT_COMPONENTS})
message(STATUS "Vocal Ink: building against Qt ${Qt6_VERSION}")

set(CMAKE_AUTOMOC ON)
if(QT_KNOWN_POLICY_QTP0001)
    qt_policy(SET QTP0001 NEW)
endif()
set(CMAKE_AUTORCC ON)

include(cmake/Dependencies.cmake)

add_subdirectory(src)

if(VOCALINK_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

include(cmake/Install.cmake)
