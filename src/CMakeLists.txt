# vocalink_core: everything except the widgets UI. The app and the tests link it.
set(VOCALINK_CORE_DIRS app core audio tts stt models obs platform)
set(VOCALINK_CORE_SOURCES)
foreach(dir IN LISTS VOCALINK_CORE_DIRS)
    file(GLOB _dir_sources CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/${dir}/*.cpp"
        "${CMAKE_CURRENT_SOURCE_DIR}/${dir}/*.h")
    list(APPEND VOCALINK_CORE_SOURCES ${_dir_sources})
endforeach()

add_library(vocalink_core STATIC
    ${VOCALINK_CORE_SOURCES}
    ${PROJECT_SOURCE_DIR}/resources/core.qrc)

configure_file(Version.h.in ${CMAKE_CURRENT_BINARY_DIR}/generated/Version.h @ONLY)

target_include_directories(vocalink_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_BINARY_DIR}/generated)

target_link_libraries(vocalink_core PUBLIC
    Qt6::Core Qt6::Gui Qt6::Network Qt6::Multimedia
    Qt6::TextToSpeech Qt6::WebSockets Qt6::Concurrent)

target_compile_definitions(vocalink_core PUBLIC
    QT_NO_CAST_FROM_BYTEARRAY
    QT_NO_URL_CAST_FROM_STRING)

if(MSVC)
    target_compile_options(vocalink_core PUBLIC /utf-8 /W3 /permissive-)
else()
    target_compile_options(vocalink_core PRIVATE -Wall -Wextra)
endif()

if(VOCALINK_WITH_WHISPER)
    target_link_libraries(vocalink_core PUBLIC whisper)
    target_compile_definitions(vocalink_core PUBLIC VOCALINK_HAVE_WHISPER=1)
endif()
if(VOCALINK_WITH_HOTKEYS)
    target_link_libraries(vocalink_core PUBLIC qhotkey)
    target_compile_definitions(vocalink_core PUBLIC VOCALINK_HAVE_HOTKEYS=1)
endif()
if(VOCALINK_WITH_KEYCHAIN)
    target_link_libraries(vocalink_core PUBLIC qt6keychain)
    target_compile_definitions(vocalink_core PUBLIC VOCALINK_HAVE_KEYCHAIN=1)
endif()

# --- The application ----------------------------------------------------------
file(GLOB VOCALINK_UI_SOURCES CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/ui/*.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/ui/*.h")

set(VOCALINK_APP_EXTRA)
if(WIN32)
    configure_file(${PROJECT_SOURCE_DIR}/packaging/windows/VocalInk.rc.in
                   ${CMAKE_CURRENT_BINARY_DIR}/generated/VocalInk.rc @ONLY)
    list(APPEND VOCALINK_APP_EXTRA ${CMAKE_CURRENT_BINARY_DIR}/generated/VocalInk.rc)
elseif(APPLE)
    set(VOCALINK_ICNS ${PROJECT_SOURCE_DIR}/resources/icons/VocalInk.icns)
    set_source_files_properties(${VOCALINK_ICNS} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    list(APPEND VOCALINK_APP_EXTRA ${VOCALINK_ICNS})
endif()

add_executable(VocalInk WIN32 MACOSX_BUNDLE
    main.cpp
    ${VOCALINK_UI_SOURCES}
    ${PROJECT_SOURCE_DIR}/resources/ui.qrc
    ${VOCALINK_APP_EXTRA})

target_link_libraries(VocalInk PRIVATE vocalink_core Qt6::Widgets)

if(NOT MSVC)
    target_compile_options(VocalInk PRIVATE -Wall -Wextra)
endif()

if(APPLE)
    set_target_properties(VocalInk PROPERTIES
        MACOSX_BUNDLE_INFO_PLIST ${PROJECT_SOURCE_DIR}/packaging/macos/Info.plist.in
        MACOSX_BUNDLE_BUNDLE_NAME "${VOCALINK_DISPLAY_NAME}"
        MACOSX_BUNDLE_GUI_IDENTIFIER "${VOCALINK_BUNDLE_ID}"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_ICON_FILE VocalInk.icns)
endif()
