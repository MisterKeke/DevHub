qt_add_executable(devhub_smoke_tests smoke_test.cpp)
target_link_libraries(devhub_smoke_tests PRIVATE Qt6::Core Qt6::Test project_options project_warnings)
add_test(NAME devhub.smoke COMMAND devhub_smoke_tests)
set_tests_properties(devhub.smoke PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
)

qt_add_executable(devhub_window_shutdown_driver helpers/shutdown_window_driver.cpp)
target_link_libraries(devhub_window_shutdown_driver PRIVATE devhub_core Qt6::Widgets project_warnings)
qt_add_executable(devhub_shutdown_tests shutdown_test.cpp)
target_link_libraries(devhub_shutdown_tests PRIVATE Qt6::Core Qt6::Test project_options project_warnings)
target_compile_definitions(devhub_shutdown_tests PRIVATE
    DEVHUB_WINDOW_SHUTDOWN_DRIVER="$<TARGET_FILE:devhub_window_shutdown_driver>")
add_dependencies(devhub_shutdown_tests devhub_window_shutdown_driver)
add_test(NAME devhub.shutdown COMMAND devhub_shutdown_tests
    -o "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/shutdown-tests.txt,txt")
set_tests_properties(devhub.shutdown PROPERTIES TIMEOUT 30
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt6::Core>/../plugins"
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")

qt_add_executable(devhub_fake_codex helpers/fake_codex_app_server.cpp)
target_link_libraries(devhub_fake_codex PRIVATE Qt6::Core project_options project_warnings)
qt_add_executable(devhub_assistant_native_probe helpers/assistant_native_probe.cpp)
target_link_libraries(devhub_assistant_native_probe PRIVATE devhub_assistant project_warnings)
# Deliberately absent from CTest: opt-in installed-native check, no inference/auth.
qt_add_executable(devhub_assistant_shutdown_driver helpers/assistant_shutdown_driver.cpp)
target_link_libraries(devhub_assistant_shutdown_driver PRIVATE devhub_assistant project_warnings)
target_compile_definitions(devhub_assistant_shutdown_driver PRIVATE
    DEVHUB_FAKE_CODEX="$<TARGET_FILE:devhub_fake_codex>")
add_dependencies(devhub_assistant_shutdown_driver devhub_fake_codex)

qt_add_executable(devhub_assistant_protocol_tests assistant_protocol_test.cpp)
target_link_libraries(devhub_assistant_protocol_tests PRIVATE devhub_assistant Qt6::Test project_warnings)
add_test(NAME devhub.assistant-protocol COMMAND devhub_assistant_protocol_tests
    -o "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/assistant-protocol-tests.txt,txt")
qt_add_executable(devhub_assistant_process_tests assistant_process_test.cpp)
target_link_libraries(devhub_assistant_process_tests PRIVATE devhub_assistant Qt6::Test project_warnings)
target_compile_definitions(devhub_assistant_process_tests PRIVATE
    DEVHUB_FAKE_CODEX="$<TARGET_FILE:devhub_fake_codex>"
    DEVHUB_SHUTDOWN_DRIVER="$<TARGET_FILE:devhub_assistant_shutdown_driver>")
add_dependencies(devhub_assistant_process_tests devhub_fake_codex devhub_assistant_shutdown_driver)
add_test(NAME devhub.assistant-process COMMAND devhub_assistant_process_tests
    -o "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/assistant-process-tests.txt,txt")
qt_add_executable(devhub_assistant_client_tests assistant_client_test.cpp)
target_link_libraries(devhub_assistant_client_tests PRIVATE devhub_assistant Qt6::Test project_warnings)
add_test(NAME devhub.assistant-client COMMAND devhub_assistant_client_tests
    -o "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/assistant-client-tests.txt,txt")
set_tests_properties(devhub.assistant-protocol devhub.assistant-process devhub.assistant-client PROPERTIES
    TIMEOUT 45
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt6::Core>/../plugins"
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
)
qt_add_executable(devhub_assistant_controller_tests assistant_controller_test.cpp)
target_link_libraries(devhub_assistant_controller_tests PRIVATE devhub_assistant Qt6::Test project_warnings)
add_test(NAME devhub.assistant-controller COMMAND devhub_assistant_controller_tests
    -o "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/assistant-controller-tests.txt,txt")
qt_add_executable(devhub_assistant_ui_tests assistant_ui_test.cpp
    ../src/application/commands/CommandRegistry.cpp
    ../src/presentation/assistant/AssistantPage.cpp
    ../src/presentation/widgets/SafeMarkdownBrowser.cpp
    ../src/presentation/theme/ThemeTokens.cpp
    ../src/presentation/theme/ThemeManager.cpp
    ../src/presentation/theme/ThemePalette.cpp
    ../src/presentation/theme/ThemeProxyStyle.cpp
)
target_link_libraries(devhub_assistant_ui_tests PRIVATE devhub_assistant Qt6::Widgets Qt6::Test project_warnings)
add_test(NAME devhub.assistant-ui COMMAND devhub_assistant_ui_tests
    -o "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/assistant-ui-tests.txt,txt")
set_tests_properties(devhub.assistant-controller devhub.assistant-ui PROPERTIES TIMEOUT 45
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt6::Core>/../plugins"
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
)
qt_add_executable(devhub_assistant_conversation_repository_tests
    assistant_conversation_repository_test.cpp
    ../src/domain/account/AccountId.cpp
    ../src/infrastructure/notes/NotesRepository.cpp
    ../resources/database_migrations.qrc)
target_link_libraries(devhub_assistant_conversation_repository_tests PRIVATE
    devhub_assistant devhub_database Qt6::Core Qt6::Sql Qt6::Test project_options project_warnings)
add_test(NAME devhub.assistant-conversation-repository COMMAND devhub_assistant_conversation_repository_tests)
set_tests_properties(devhub.assistant-conversation-repository PROPERTIES
    TIMEOUT 45 ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt6::Core>/../plugins"
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")

qt_add_executable(devhub_assistant_settings_tests assistant_settings_test.cpp
    ../src/application/settings/SettingsService.cpp
    ../src/presentation/settings/SettingsWindow.cpp
    ../src/presentation/settings/AssistantSettingsPanel.cpp
    ../src/presentation/theme/ThemeTokens.cpp
    ../src/presentation/theme/ThemeManager.cpp
    ../src/presentation/theme/ThemePalette.cpp
    ../src/presentation/theme/ThemeProxyStyle.cpp)
target_link_libraries(devhub_assistant_settings_tests PRIVATE devhub_assistant Qt6::Widgets Qt6::Test project_warnings)
add_test(NAME devhub.assistant-settings COMMAND devhub_assistant_settings_tests
    -o "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/assistant-settings-tests.txt,txt")
set_tests_properties(devhub.assistant-settings PROPERTIES TIMEOUT 45
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt6::Core>/../plugins"
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")

qt_add_executable(devhub_assistant_tools_tests assistant_tools_test.cpp)
target_link_libraries(devhub_assistant_tools_tests PRIVATE devhub_assistant Qt6::Test project_warnings)
add_test(NAME devhub.assistant-tools COMMAND devhub_assistant_tools_tests
    -o "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/assistant-tools-tests.txt,txt")
set_tests_properties(devhub.assistant-tools PROPERTIES TIMEOUT 45
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")

qt_add_executable(devhub_assistant_tool_runtime_tests assistant_tool_runtime_test.cpp)
target_link_libraries(devhub_assistant_tool_runtime_tests PRIVATE devhub_assistant Qt6::Test project_warnings)
add_test(NAME devhub.assistant-tool-runtime COMMAND devhub_assistant_tool_runtime_tests
    -o "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/assistant-tool-runtime-tests.txt,txt")
set_tests_properties(devhub.assistant-tool-runtime PROPERTIES TIMEOUT 45
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt6::Core>/../plugins;TMP=${CMAKE_CURRENT_BINARY_DIR};TEMP=${CMAKE_CURRENT_BINARY_DIR}"
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")

qt_add_executable(devhub_file_browser_ui_tests
    file_browser_ui_test.cpp
    ../src/presentation/files/FileBrowserPage.h
    ../src/presentation/files/FileBrowserPage.cpp
    ../src/presentation/files/FileBrowserController.h
    ../src/presentation/files/FileBrowserController.cpp)
target_link_libraries(devhub_file_browser_ui_tests PRIVATE devhub_file_services Qt6::Widgets Qt6::Concurrent Qt6::Test project_warnings)
add_test(NAME devhub.file-browser-ui COMMAND devhub_file_browser_ui_tests)
set_tests_properties(devhub.file-browser-ui PROPERTIES TIMEOUT 30
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt6::Core>/../plugins"
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")

add_library(devhub_notes_ui_test_support OBJECT
    ../src/presentation/notes/NotesPage.h
    ../src/presentation/notes/NotesPage.cpp
    ../src/presentation/widgets/SafeMarkdownBrowser.cpp
    ../src/application/notes/NoteFolderExporter.cpp)
target_link_libraries(devhub_notes_ui_test_support PUBLIC devhub_file_services Qt6::Widgets Qt6::Concurrent)

qt_add_executable(devhub_notes_search_ui_tests
    notes_search_ui_test.cpp
    $<TARGET_OBJECTS:devhub_notes_ui_test_support>)
target_link_libraries(devhub_notes_search_ui_tests PRIVATE devhub_file_services Qt6::Widgets Qt6::Concurrent Qt6::Test project_warnings)
add_test(NAME devhub.notes-search-ui COMMAND devhub_notes_search_ui_tests)
set_tests_properties(devhub.notes-search-ui PROPERTIES TIMEOUT 30
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt6::Core>/../plugins"
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>")

# Developer-only harness; intentionally excluded from CTest and default builds.
if(DEVHUB_BUILD_LIVE_ASSISTANT_TESTS)
    # Offline-only policy test for the compile-time validation archive. This
    # executable does not launch Codex, authenticate, or create a thread.
    qt_add_executable(devhub_assistant_live_validation_policy_tests
        assistant_live_validation_policy_test.cpp)
    target_link_libraries(devhub_assistant_live_validation_policy_tests PRIVATE
        devhub_assistant_live_validation Qt6::Test project_warnings)

    qt_add_executable(devhub_assistant_live_tests assistant_live_integration_test.cpp)
    target_link_libraries(devhub_assistant_live_tests PRIVATE
        devhub_assistant_live_validation Qt6::Test project_warnings)
endif()

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
target_link_libraries(devhub_feature_tests PRIVATE devhub_services devhub_database devhub_assistant
    Qt6::Core Qt6::Widgets Qt6::Sql Qt6::Test project_options project_warnings)
add_test(NAME devhub.feature-regressions COMMAND devhub_feature_tests
    -o "${CMAKE_CURRENT_BINARY_DIR}/$<CONFIG>/feature-regressions-tests.txt,txt")
set_tests_properties(devhub.feature-regressions PROPERTIES
    TIMEOUT 45
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    ENVIRONMENT_MODIFICATION "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;PATH=path_list_prepend:$<IF:$<CONFIG:Debug>,${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/debug/bin,${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/bin>"
)

qt_add_executable(devhub_reliability_tests
    reliability_test.cpp
    $<TARGET_OBJECTS:devhub_notes_ui_test_support>
    ../resources/database_migrations.qrc
)
target_link_libraries(devhub_reliability_tests PRIVATE devhub_accounts devhub_ports devhub_file_services
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
