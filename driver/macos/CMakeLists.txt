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
# Built as a separate project and installed into the build tree, the way libASPL's
# README documents it: its CMakeLists isn't meant to be a subproject (as one, ninja
# rejects it: "_deps/libaspl-build is defined as an output multiple times").
# It must be cloned from the tag with full history: its CMakeLists runs
# `git describe --tags`, and v3.1.2 isn't a branch tip, so a shallow clone fails.
include(ExternalProject)
set(_vi_aspl_prefix ${CMAKE_CURRENT_BINARY_DIR}/libaspl)
set(_vi_aspl_lib ${_vi_aspl_prefix}/lib/${CMAKE_STATIC_LIBRARY_PREFIX}ASPL${CMAKE_STATIC_LIBRARY_SUFFIX})
string(REPLACE ";" "$<SEMICOLON>" _vi_aspl_archs "${CMAKE_OSX_ARCHITECTURES}")
ExternalProject_Add(libaspl_project
    GIT_REPOSITORY https://github.com/gavv/libASPL.git
    GIT_TAG        v3.1.2
    GIT_SHALLOW    FALSE
    GIT_PROGRESS   FALSE
    UPDATE_COMMAND ""
    PREFIX         ${CMAKE_CURRENT_BINARY_DIR}/libaspl-work
    INSTALL_DIR    ${_vi_aspl_prefix}
    CMAKE_ARGS
        -DCMAKE_INSTALL_PREFIX=<INSTALL_DIR>
        -DCMAKE_INSTALL_LIBDIR=lib
        -DCMAKE_BUILD_TYPE=Release
        -DCMAKE_OSX_ARCHITECTURES=${_vi_aspl_archs}
        -DCMAKE_OSX_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET}
        -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
        -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
        -DBUILD_TESTING=OFF
        -DBUILD_DOCUMENTATION=OFF
    BUILD_BYPRODUCTS ${_vi_aspl_lib})

# The headers only exist after the external build; the directory must exist now.
file(MAKE_DIRECTORY ${_vi_aspl_prefix}/include)
add_library(libASPL STATIC IMPORTED GLOBAL)
set_target_properties(libASPL PROPERTIES
    IMPORTED_LOCATION ${_vi_aspl_lib}
    INTERFACE_INCLUDE_DIRECTORIES ${_vi_aspl_prefix}/include
    INTERFACE_LINK_LIBRARIES "-framework CoreFoundation;-framework CoreAudio")
add_dependencies(libASPL libaspl_project)

# --- The plug-in bundle -------------------------------------------------------------
# A MODULE built as a CFBundle: VocalInkVirtualMic.driver/Contents/{Info.plist,MacOS/VocalInkVirtualMic}.
add_library(VocalInkVirtualMic MODULE
    VocalInkVirtualMic.cpp
    LoopbackRing.h
    Info.plist.in)
target_compile_features(VocalInkVirtualMic PRIVATE cxx_std_17)
target_compile_options(VocalInkVirtualMic PRIVATE -Wall -Wextra)
target_link_libraries(VocalInkVirtualMic PRIVATE libASPL)
add_dependencies(VocalInkVirtualMic libaspl_project)
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
