# vocalink_core: everything except the widgets UI. The app and the tests link it.
set(VOCALINK_CORE_DIRS app core audio tts stt models obs platform avatar)
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

target_link_libraries(VocalInk PRIVATE vocalink_core Qt6::Widgets Qt6::Qml Qt6::Quick Qt6::QuickControls2)

# The interface is Qt Quick: every .qml file in qml/ is compiled into the
# "Ink" module. Keep the directory flat (one module, no sub-imports).
file(GLOB VOCALINK_QML_FILES CONFIGURE_DEPENDS
    RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
    "${CMAKE_CURRENT_SOURCE_DIR}/qml/*.qml"
    "${CMAKE_CURRENT_SOURCE_DIR}/qml/*.js")
set_source_files_properties(qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
foreach(f IN LISTS VOCALINK_QML_FILES)
    get_filename_component(_name ${f} NAME)
    set_source_files_properties(${f} PROPERTIES QT_RESOURCE_ALIAS ${_name})
endforeach()

qt_add_qml_module(VocalInk
    URI Ink
    VERSION 1.0
    RESOURCE_PREFIX /qt/qml
    QML_FILES ${VOCALINK_QML_FILES})

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

# --- Translations ---------------------------------------------------------------
# i18n/vocalink_<lang>.ts are compiled into the app as :/i18n/vocalink_<lang>.qm.
# After changing interface text, refresh them from the sources with
#   cmake --build build --target update_translations
# (tools/i18n.py helps translate what's new; see i18n/README.md).
if(VOCALINK_WITH_TRANSLATIONS)
    find_package(Qt6 QUIET COMPONENTS LinguistTools)
endif()
if(VOCALINK_WITH_TRANSLATIONS AND Qt6LinguistTools_FOUND)
    set(VOCALINK_LANGUAGES en es fr de pt_BR it nl pl tr ja ko zh_CN)
    set(VOCALINK_TS_FILES)
    foreach(lang IN LISTS VOCALINK_LANGUAGES)
        list(APPEND VOCALINK_TS_FILES ${PROJECT_SOURCE_DIR}/i18n/vocalink_${lang}.ts)
    endforeach()
    # Everything with interface text. TextProcessor.cpp stays out: its strings
    # (emoji names, "link") are spoken, so they follow the voice's language.
    set(VOCALINK_I18N_SOURCES main.cpp ${VOCALINK_UI_SOURCES} ${VOCALINK_CORE_SOURCES} ${VOCALINK_QML_FILES})
    list(FILTER VOCALINK_I18N_SOURCES EXCLUDE REGEX "TextProcessor\\.cpp$")
    qt_add_translations(VocalInk
        TS_FILES ${VOCALINK_TS_FILES}
        SOURCES ${VOCALINK_I18N_SOURCES}
        RESOURCE_PREFIX /i18n
        LUPDATE_OPTIONS -locations relative -no-obsolete
        LRELEASE_OPTIONS -silent)

    # Qt's own translations (key names like "Strg", standard dialogs), when this
    # Qt install has them.
    set(_vi_qt_qm_dir "${QT6_INSTALL_PREFIX}/${QT6_INSTALL_TRANSLATIONS}")
    set(_vi_qt_qm)
    foreach(lang IN LISTS VOCALINK_LANGUAGES)
        foreach(module IN ITEMS qtbase qtdeclarative)
            if(NOT lang STREQUAL "en" AND EXISTS "${_vi_qt_qm_dir}/${module}_${lang}.qm")
                list(APPEND _vi_qt_qm "${_vi_qt_qm_dir}/${module}_${lang}.qm")
            endif()
        endforeach()
    endforeach()
    if(_vi_qt_qm)
        qt_add_resources(VocalInk vocalink_qt_translations PREFIX /i18n BASE "${_vi_qt_qm_dir}" FILES ${_vi_qt_qm})
    endif()
    list(LENGTH _vi_qt_qm _vi_qt_qm_count)
    message(STATUS "Translations: ${VOCALINK_LANGUAGES} (+${_vi_qt_qm_count} Qt catalogs)")
elseif(VOCALINK_WITH_TRANSLATIONS)
    message(WARNING "Qt LinguistTools not found: building without translations (English only).")
endif()
