qt_add_executable(easel WIN32 MACOSX_BUNDLE
    main.cpp
)

qt_add_resources(easel "icons"
    PREFIX "/icons"
    BASE icons
    FILES icons/easel.png
)

target_link_libraries(easel PRIVATE easel_ui)

set_target_properties(easel PROPERTIES
    MACOSX_BUNDLE_BUNDLE_NAME "Easel"
    MACOSX_BUNDLE_GUI_IDENTIFIER "io.github.scottpeterman.easel"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
)
if(APPLE)
    set_target_properties(easel PROPERTIES OUTPUT_NAME "Easel")
endif()

install(TARGETS easel
    BUNDLE DESTINATION .
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

if(WIN32 OR APPLE)
    # windeployqt / macdeployqt run at install time. Requires Qt 6.5+.
    qt_generate_deploy_app_script(
        TARGET easel
        OUTPUT_SCRIPT easel_deploy_script
        NO_UNSUPPORTED_PLATFORM_ERROR
    )
    install(SCRIPT ${easel_deploy_script})
endif()

if(WIN32)
    # Ship the MSVC runtime so users don't need the VC++ redistributable.
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_DESTINATION ${CMAKE_INSTALL_BINDIR})
    include(InstallRequiredSystemLibraries)
endif()

if(UNIX AND NOT APPLE)
    install(FILES ${PROJECT_SOURCE_DIR}/packaging/linux/easel.desktop
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES icons/easel.png
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/256x256/apps)
endif()
