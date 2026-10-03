include(GNUInstallDirs)

install(TARGETS devhub RUNTIME DESTINATION "bin")
install(FILES
    "${CMAKE_CURRENT_SOURCE_DIR}/README.md"
    "${CMAKE_CURRENT_SOURCE_DIR}/DEVHUB_MASTER_DEVELOPMENT_PLAN.md"
    DESTINATION ".")
install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/docs/"
    DESTINATION "docs"
    FILES_MATCHING PATTERN "*.md")
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/packaging/README.md"
    DESTINATION "packaging")

if(WIN32)
    qt_generate_deploy_app_script(
        TARGET devhub
        OUTPUT_SCRIPT devhub_deploy_script
        INCLUDE_PLUGINS qsqlite
        DEPLOY_TOOL_OPTIONS --qmldir "${CMAKE_CURRENT_SOURCE_DIR}/resources/qml"
        NO_UNSUPPORTED_PLATFORM_ERROR)
    install(SCRIPT "${devhub_deploy_script}")

    # Dynamic MinGW triplets need the libsodium DLL beside the executable.
    # The default static-library triplet has no DLL to copy.
    get_target_property(_devhub_sodium_release unofficial-sodium::sodium IMPORTED_LOCATION_RELEASE)
    if(_devhub_sodium_release)
        get_filename_component(_devhub_sodium_lib_dir "${_devhub_sodium_release}" DIRECTORY)
        get_filename_component(_devhub_sodium_triplet_dir "${_devhub_sodium_lib_dir}" DIRECTORY)
        unset(_devhub_sodium_runtime CACHE)
        find_file(_devhub_sodium_runtime
            NAMES libsodium-26.dll libsodium.dll
            PATHS "${_devhub_sodium_triplet_dir}/bin"
            NO_DEFAULT_PATH)
        if(_devhub_sodium_runtime)
            install(FILES "${_devhub_sodium_runtime}" DESTINATION "bin")
        endif()
    endif()
    get_target_property(_devhub_sqlite_release unofficial::sqlite3::sqlite3 IMPORTED_LOCATION_RELEASE)
    if(_devhub_sqlite_release)
        get_filename_component(_devhub_sqlite_lib_dir "${_devhub_sqlite_release}" DIRECTORY)
        get_filename_component(_devhub_sqlite_triplet_dir "${_devhub_sqlite_lib_dir}" DIRECTORY)
        find_file(_devhub_sqlite_runtime NAMES sqlite3.dll
            PATHS "${_devhub_sqlite_triplet_dir}/bin" NO_DEFAULT_PATH NO_CACHE)
        if(_devhub_sqlite_runtime)
            install(FILES "${_devhub_sqlite_runtime}" DESTINATION "bin")
        endif()
    endif()
endif()
