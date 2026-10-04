qt_add_executable(devhub_smoke_tests smoke_test.cpp)
target_link_libraries(devhub_smoke_tests PRIVATE Qt6::Core Qt6::Test project_options project_warnings)
add_test(NAME devhub.smoke COMMAND devhub_smoke_tests)
set_tests_properties(devhub.smoke PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
)

qt_add_executable(devhub_database_tests
    database_migration_test.cpp
    ../resources/database_migrations.qrc
)
target_link_libraries(devhub_database_tests PRIVATE
    devhub_database
    Qt6::Core
    Qt6::Sql
    Qt6::Test
    project_options
    project_warnings
)
add_test(NAME devhub.database-migrations COMMAND devhub_database_tests)
set_tests_properties(devhub.database-migrations PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
)

qt_add_executable(devhub_account_tests
    account_security_test.cpp
    ../resources/database_migrations.qrc
)
target_link_libraries(devhub_account_tests PRIVATE
    devhub_accounts
    Qt6::Core
    Qt6::Sql
    Qt6::Test
    project_options
    project_warnings
)
add_test(NAME devhub.account-security COMMAND devhub_account_tests)
set_tests_properties(devhub.account-security PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:$<IF:$<CONFIG:Debug>,${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/bin,${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin>"
)

qt_add_executable(devhub_authentication_tests
    authentication_test.cpp
    ../resources/database_migrations.qrc
    ../src/presentation/authentication/AuthenticationWidgets.cpp
    ../src/presentation/widgets/ReusableWidgets.cpp
    ../src/presentation/icons/IconLoader.cpp
    ../src/presentation/theme/ThemePalette.cpp
    ../src/presentation/theme/ThemeTokens.cpp
    ../resources/devhub.qrc
)
target_link_libraries(devhub_authentication_tests PRIVATE
    devhub_accounts
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Sql
    Qt6::Svg
    Qt6::Test
    project_options
    project_warnings
)
add_test(NAME devhub.authentication COMMAND devhub_authentication_tests)
set_tests_properties(devhub.authentication PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:$<IF:$<CONFIG:Debug>,${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/bin,${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin>"
)
qt_add_executable(devhub_feature_tests
    feature_regression_test.cpp
    ../src/presentation/databases/DatabasePage.cpp
    ../src/presentation/databases/DatabaseTableModel.cpp
    ../src/application/settings/SettingsService.cpp
    ../src/application/support/SupportBundleBuilder.cpp
)
target_link_libraries(devhub_feature_tests PRIVATE devhub_services devhub_database
    Qt6::Core Qt6::Widgets Qt6::Sql Qt6::Test project_options project_warnings)
add_test(NAME devhub.feature-regressions COMMAND devhub_feature_tests)
set_tests_properties(devhub.feature-regressions PROPERTIES
    TIMEOUT 45
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:$<IF:$<CONFIG:Debug>,${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/bin,${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin>"
)

qt_add_executable(devhub_reliability_tests
    reliability_test.cpp
    ../src/presentation/notes/NotesPage.cpp
    ../src/application/notes/NoteFolderExporter.cpp
    ../resources/database_migrations.qrc
)
target_link_libraries(devhub_reliability_tests PRIVATE devhub_accounts devhub_ports
    Qt6::Widgets Qt6::Test project_options project_warnings)
add_test(NAME devhub.reliability COMMAND devhub_reliability_tests
    -o "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/reliability-tests.txt,txt")
set_tests_properties(devhub.reliability PROPERTIES TIMEOUT 45
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:$<IF:$<CONFIG:Debug>,${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/bin,${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin>"
)

qt_add_executable(devhub_runtime_tests runtime_check_test.cpp)
target_link_libraries(devhub_runtime_tests PRIVATE Qt6::Core Qt6::Test project_options project_warnings)
target_compile_definitions(devhub_runtime_tests PRIVATE
    DEVHUB_TEST_EXECUTABLE="$<TARGET_FILE:devhub>"
    DEVHUB_TEST_QML_SOURCE="${CMAKE_SOURCE_DIR}/resources/qml"
)
add_dependencies(devhub_runtime_tests devhub)
add_test(NAME devhub.qml-runtime COMMAND devhub_runtime_tests
    -o "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/qml-runtime-tests.txt,txt")
set_tests_properties(devhub.qml-runtime PROPERTIES TIMEOUT 45
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:$<IF:$<CONFIG:Debug>,${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/bin,${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin>"
)

# Development test folders can contain copied Qt DLLs. Point plugin discovery
# at the configured Qt kit rather than relying on the DLLs' compiled prefix.
set_tests_properties(devhub.smoke devhub.database-migrations devhub.account-security
    devhub.authentication devhub.feature-regressions devhub.reliability devhub.qml-runtime PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt6::Core>/../plugins"
)
