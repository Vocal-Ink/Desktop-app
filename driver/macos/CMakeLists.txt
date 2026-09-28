# VocalInkVirtualMic.driver: the macOS virtual mic, a Core Audio HAL plug-in built on
# libASPL (MIT). See VocalInkVirtualMic.cpp and docs/VIRTUAL_AUDIO.md.
#
# Built when VOCALINK_WITH_MAC_DRIVER is ON. With VOCALINK_DRIVER_ONLY=ON this is the
# only thing configured (no Qt needed):
#   cmake -S . -B build-driver -DVOCALINK_DRIVER_ONLY=ON -DVOCALINK_WITH_MAC_DRIVER=ON
#   cmake --build build-driver --target VocalInkVirtualMic

# Bump when the driver changes: the app compares it with the installed copy and
# offers the update.
set(VOCALINK_MAC_DRIVER_VERSION "1.0.0")
# Fixed forever: names the plug-in factory in Info.plist.
set(VOCALINK_MAC_DRIVER_FACTORY_UUID "F51560CD-050C-4212-99CF-F4208A7F5A67")
set(VOCALINK_MAC_DRIVER_SIGN_IDENTITY "-" CACHE STRING
    "codesign identity for VocalInkVirtualMic.driver (\"-\" = ad hoc; a Developer ID Application identity for releases)")

# Nothing here uses Qt.
set(CMAKE_AUTOMOC OFF)
set(CMAKE_AUTOUIC OFF)
set(CMAKE_AUTORCC OFF)

# --- libASPL v3.1.2 ---------------------------------------------------------------
# Must be fetched from a tag with full history: libASPL's CMakeLists runs
# `git describe --tags` and parses the result, and v3.1.2 isn't a branch tip, so a
# shallow clone can't check it out. Its targets (`compile_commands` is ALL) and
# install rules stay out of the build via EXCLUDE_FROM_ALL.
include(FetchContent)
set(BUILD_TESTING OFF)
set(BUILD_DOCUMENTATION OFF)
set(_vi_aspl_source
    GIT_REPOSITORY https://github.com/gavv/libASPL.git
    GIT_TAG        v3.1.2
    GIT_SHALLOW    FALSE
    GIT_PROGRESS   FALSE)
if(CMAKE_VERSION VERSION_GREATER_EQUAL 3.28)
    FetchContent_Declare(libaspl ${_vi_aspl_source} EXCLUDE_FROM_ALL)
    FetchContent_MakeAvailable(libaspl)
else()
    FetchContent_Declare(libaspl ${_vi_aspl_source})
    FetchContent_GetProperties(libaspl)
    if(NOT libaspl_POPULATED)
        FetchContent_Populate(libaspl)
        add_subdirectory(${libaspl_SOURCE_DIR} ${libaspl_BINARY_DIR} EXCLUDE_FROM_ALL)
    endif()
endif()

# --- The plug-in bundle -------------------------------------------------------------
# A MODULE built as a CFBundle: VocalInkVirtualMic.driver/Contents/{Info.plist,MacOS/VocalInkVirtualMic}.
add_library(VocalInkVirtualMic MODULE
    VocalInkVirtualMic.cpp
    LoopbackRing.h
    Info.plist.in)
target_compile_features(VocalInkVirtualMic PRIVATE cxx_std_17)
target_compile_options(VocalInkVirtualMic PRIVATE -Wall -Wextra)
target_link_libraries(VocalInkVirtualMic PRIVATE libASPL)
# coreaudiod only needs the entry point; keep everything else (libASPL included) private.
target_link_options(VocalInkVirtualMic PRIVATE "LINKER:-exported_symbol,_VocalInkVirtualMicEntryPoint")
set_target_properties(VocalInkVirtualMic PROPERTIES
    BUNDLE TRUE
    BUNDLE_EXTENSION driver
    PREFIX ""
    SUFFIX ""
    OUTPUT_NAME VocalInkVirtualMic
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN ON
    MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in
    MACOSX_BUNDLE_BUNDLE_NAME "Vocal Ink Virtual Mic"
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.vocalink.VirtualMic.driver"
    MACOSX_BUNDLE_BUNDLE_VERSION "${VOCALINK_MAC_DRIVER_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${VOCALINK_MAC_DRIVER_VERSION}"
    MACOSX_BUNDLE_COPYRIGHT "Copyright (c) Vocal Ink contributors. Apache-2.0; uses libASPL (MIT).")

# Sign right after linking; the app is signed later (inside-out). Hardened runtime and
# a secure timestamp only for a real identity.
if(VOCALINK_MAC_DRIVER_SIGN_IDENTITY STREQUAL "-")
    set(_vi_sign_args --sign - --timestamp=none)
else()
    set(_vi_sign_args --sign "${VOCALINK_MAC_DRIVER_SIGN_IDENTITY}" --options runtime --timestamp)
endif()
add_custom_command(TARGET VocalInkVirtualMic POST_BUILD
    COMMAND codesign --force ${_vi_sign_args} "$<TARGET_BUNDLE_DIR:VocalInkVirtualMic>"
    COMMAND codesign --verify --strict --verbose=2 "$<TARGET_BUNDLE_DIR:VocalInkVirtualMic>"
    COMMENT "Signing VocalInkVirtualMic.driver"
    VERBATIM)

# In a full build, ship the plug-in inside the app: VocalInk.app/Contents/Resources
# (not Contents/PlugIns, which macdeployqt manages). VirtualDriver installs it from there.
if(TARGET VocalInk)
    add_custom_target(VocalInkVirtualMicInApp ALL
        COMMAND ${CMAKE_COMMAND} -E rm -rf "$<TARGET_BUNDLE_CONTENT_DIR:VocalInk>/Resources/VocalInkVirtualMic.driver"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "$<TARGET_BUNDLE_DIR:VocalInkVirtualMic>"
                "$<TARGET_BUNDLE_CONTENT_DIR:VocalInk>/Resources/VocalInkVirtualMic.driver"
        COMMENT "Copying VocalInkVirtualMic.driver into VocalInk.app"
        VERBATIM)
    add_dependencies(VocalInkVirtualMicInApp VocalInk VocalInkVirtualMic)
endif()
