# Install rules (used by `cmake --install` and the Linux AppImage build).
include(GNUInstallDirs)

install(TARGETS VocalInk
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

if(UNIX AND NOT APPLE)
    install(FILES ${PROJECT_SOURCE_DIR}/packaging/linux/org.vocalink.desktop.desktop
            DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES ${PROJECT_SOURCE_DIR}/packaging/linux/org.vocalink.desktop.metainfo.xml
            DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
    foreach(size 64 128 256 512)
        install(FILES ${PROJECT_SOURCE_DIR}/resources/icons/app-${size}.png
                DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/${size}x${size}/apps
                RENAME org.vocalink.desktop.png)
    endforeach()
endif()
