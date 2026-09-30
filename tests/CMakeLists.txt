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
