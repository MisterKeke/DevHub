cmake_minimum_required(VERSION 3.28)

# vcpkg selects its MinGW compilers from PATH independently of the application
# compiler supplied by Qt Creator. Put that kit first before project() triggers
# the manifest install, so cached dependencies use the same compiler/runtime.
if(CMAKE_HOST_WIN32 AND VCPKG_TARGET_TRIPLET MATCHES "-mingw-" AND
   IS_ABSOLUTE "${CMAKE_CXX_COMPILER}")
    get_filename_component(_devhub_compiler_bin "${CMAKE_CXX_COMPILER}" DIRECTORY)
    if(EXISTS "${_devhub_compiler_bin}/gcc.exe")
        file(TO_NATIVE_PATH "${_devhub_compiler_bin}" _devhub_compiler_bin_native)
        set(ENV{PATH} "${_devhub_compiler_bin_native};$ENV{PATH}")
    endif()
endif()

project(DevHub VERSION 0.0.2 LANGUAGES CXX)

include(CTest)
include(cmake/ProjectOptions.cmake)
include(cmake/ProjectWarnings.cmake)

option(DEVHUB_BUILD_TESTS "Build DevHub tests" ${BUILD_TESTING})
option(DEVHUB_BUILD_LIVE_ASSISTANT_TESTS "Build the explicitly opted-in real Codex integration harness" OFF)
option(DEVHUB_ENABLE_PACKAGING "Enable installer and packaging targets" OFF)
option(DEVHUB_ENABLE_SANITIZERS "Enable supported compiler sanitizers" OFF)
option(DEVHUB_WARNINGS_AS_ERRORS "Treat DevHub warnings as errors" OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.11.0 REQUIRED COMPONENTS Core Gui Widgets Sql Concurrent Svg Network Qml Quick QuickControls2)
# vcpkg's sodium config caches library paths without the triplet in the cache
# key. Clear paths left by an earlier dynamic/static triplet configuration.
foreach(_sodium_config DEBUG RELEASE)
    set(_sodium_cache_key "VCPKG_SODIUM_LIBRARY_${_sodium_config}")
    if(DEFINED ${_sodium_cache_key})
        string(REPLACE "\\" "/" _sodium_cached_path "${${_sodium_cache_key}}")
        if(NOT EXISTS "${_sodium_cached_path}" OR
           (VCPKG_TARGET_TRIPLET AND NOT _sodium_cached_path MATCHES "/${VCPKG_TARGET_TRIPLET}/"))
            unset(${_sodium_cache_key} CACHE)
        endif()
    endif()
endforeach()
find_package(unofficial-sodium CONFIG REQUIRED)
find_package(unofficial-sqlite3 CONFIG REQUIRED)

add_library(project_options INTERFACE)
target_compile_features(project_options INTERFACE cxx_std_20)
target_compile_definitions(project_options INTERFACE
    DEVHUB_VERSION="${PROJECT_VERSION}"
    DEVHUB_ORGANIZATION="DevHub"
    DEVHUB_APPLICATION_NAME="DevHub"
)

add_library(project_warnings INTERFACE)
devhub_configure_warnings(project_warnings)

if(DEVHUB_ENABLE_SANITIZERS)
    if(MSVC)
        message(FATAL_ERROR "DEVHUB_ENABLE_SANITIZERS is not supported with MSVC. Use a supported Clang toolchain.")
    endif()
    target_compile_options(project_options INTERFACE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(project_options INTERFACE -fsanitize=address,undefined)
endif()

configure_file(cmake/version.rc.in "${CMAKE_CURRENT_BINARY_DIR}/version.rc" @ONLY)
qt_policy(SET QTP0001 NEW)
qt_policy(SET QTP0004 NEW)

add_library(devhub_core STATIC
    src/application/events/ApplicationEventHub.cpp
    src/application/lifecycle/ApplicationLifecycle.cpp
    src/application/lifecycle/ShutdownCoordinator.cpp
    src/application/notifications/NotificationService.cpp
    src/application/tasks/Cancellation.cpp
    src/application/tasks/TaskScheduler.cpp
    src/domain/errors/AppError.cpp
    src/infrastructure/filesystem/ApplicationPaths.cpp
    src/presentation/errors/ErrorPresenter.cpp
    src/presentation/errors/TechnicalDetailsFormatter.cpp
    src/shared/async/Debouncer.cpp
    src/shared/async/Throttler.cpp
    src/shared/identifiers/IdGenerator.cpp
    src/shared/security/RedactionService.cpp
    src/shared/time/ApplicationClock.cpp
)
target_include_directories(devhub_core PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(devhub_core
    PUBLIC project_options
    PRIVATE project_warnings Qt6::Core
)

set(DEVHUB_ASSISTANT_SOURCES
    src/application/assistant/ICodexExecutableProbe.h
    src/application/assistant/ICodexClient.h
    src/application/assistant/IConversationRepository.h
    src/infrastructure/assistant/SqlConversationRepository.h
    src/infrastructure/assistant/SqlConversationRepository.cpp
    src/application/assistant/AssistantController.h
    src/application/assistant/AssistantController.cpp
    src/application/assistant/AssistantTypes.h
    src/application/assistant/CodexCompatibility.h
    src/application/assistant/AssistantToolRegistry.h
    src/application/assistant/AssistantToolRegistry.cpp
    src/application/assistant/AssistantToolHandlers.h
    src/application/assistant/AssistantToolHandlers.cpp
    src/application/assistant/AssistantToolRuntime.h
    src/application/assistant/AssistantToolRuntime.cpp
    src/application/assistant/AssistantApprovalCoordinator.h
    src/application/assistant/AssistantApprovalCoordinator.cpp
    src/application/assistant/ICodexTransport.h
    src/infrastructure/codex/CodexExecutableLocator.h
    src/infrastructure/codex/CodexExecutableLocator.cpp
    src/infrastructure/codex/CodexVersionSupport.h
    src/infrastructure/codex/CodexVersionSupport.cpp
    src/infrastructure/codex/CodexProtocolCodec.h
    src/infrastructure/codex/CodexProtocolCodec.cpp
    src/infrastructure/codex/CodexWriteQueue.h
    src/infrastructure/codex/CodexWriteQueue.cpp
    src/infrastructure/codex/CodexProcess.h
    src/infrastructure/codex/CodexProcess.cpp
    src/infrastructure/codex/CodexClient.h
    src/infrastructure/codex/CodexClient.cpp
    src/infrastructure/codex/CodexRuntimeProfile.h
    src/infrastructure/codex/CodexRuntimeProfile.cpp
    src/infrastructure/codex/CodexWireContract.h
    src/infrastructure/codex/CodexWireContract.cpp
    src/infrastructure/codex/CodexProtocolProfile.h
    src/infrastructure/codex/CodexProtocolProfile.cpp
    src/infrastructure/codex/CodexSchemaSurface.h
    src/infrastructure/codex/CodexSchemaSurface.cpp
    src/infrastructure/codex/CodexSchemaInspector.h
    src/infrastructure/codex/CodexSchemaInspector.cpp
    src/infrastructure/codex/CodexBehaviorVerifier.h
    src/infrastructure/codex/CodexBehaviorVerifier.cpp
    src/infrastructure/codex/CodexProcessTree.h
    src/infrastructure/codex/CodexProcessTree.cpp
    src/application/git/GitExecutableLocator.cpp
    src/application/git/GitCommandRunner.cpp
    src/application/git/GitStatusParser.cpp
    src/application/git/GitRepositoryLocator.cpp
    src/application/git/GitService.cpp
    resources/codex_contracts.qrc
    resources/codex_verification.qrc
)

add_library(devhub_file_services STATIC
    src/application/files/WorkspaceFilePolicy.h
    src/application/files/WorkspaceFilePolicy.cpp
    src/application/files/WorkspaceFileService.h
    src/application/files/WorkspaceFileService.cpp
    src/application/notes/NoteSearchService.h
    src/application/notes/NoteSearchService.cpp)
target_include_directories(devhub_file_services PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(devhub_file_services PUBLIC project_options devhub_core Qt6::Core PRIVATE project_warnings)

function(configure_devhub_assistant target)
    target_include_directories(${target} PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
    target_link_libraries(${target} PUBLIC project_options devhub_core devhub_file_services devhub_services devhub_workspaces Qt6::Core
        PRIVATE project_warnings devhub_database Qt6::Sql)
endfunction()

add_library(devhub_assistant STATIC ${DEVHUB_ASSISTANT_SOURCES})
configure_devhub_assistant(devhub_assistant)

if(DEVHUB_BUILD_LIVE_ASSISTANT_TESTS)
    # Keep the development inference-validation policy out of the production
    # library and app even when the dedicated opt-in test target is enabled.
    add_library(devhub_assistant_live_validation STATIC ${DEVHUB_ASSISTANT_SOURCES})
    configure_devhub_assistant(devhub_assistant_live_validation)
    target_compile_definitions(devhub_assistant_live_validation PRIVATE
        DEVHUB_ASSISTANT_LIVE_VALIDATION_BUILD=1)
endif()

add_library(devhub_database STATIC
    src/application/databases/DatabaseTypes.cpp
    src/application/databases/IDatabaseSession.h
    src/infrastructure/database/SqlDatabaseReferenceRepository.cpp
    src/infrastructure/database/QtSqlConnectionFactory.cpp
    src/infrastructure/database/QtSqlDatabaseSession.cpp
    src/infrastructure/database/QtSqlDatabaseWorker.cpp
    src/infrastructure/database/QtSqlDatabaseWorker.h
    src/infrastructure/database/NativeSqliteQueryExecutor.cpp
    src/infrastructure/database/DatabaseConnectionName.cpp
    src/infrastructure/database/InternalDatabaseManager.cpp
    src/infrastructure/database/SqlErrorMapper.cpp
    src/infrastructure/database/SqlTransactionGuard.cpp
)
target_include_directories(devhub_database PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(devhub_database
    PUBLIC project_options devhub_core Qt6::Core Qt6::Sql
    PRIVATE project_warnings unofficial::sqlite3::sqlite3
)

add_library(devhub_accounts STATIC
    src/application/authentication/AuthenticationProductPolicy.h
    src/application/authentication/AuthenticationService.cpp
    src/application/authentication/AuthenticationValidation.cpp
    src/application/authentication/InactivityMonitor.cpp
    src/application/authentication/IMonotonicClock.h
    src/application/authentication/SessionController.cpp
    src/application/authentication/SessionState.h
    src/application/authentication/SystemMonotonicClock.h
    src/application/accounts/IUserDirectoryProvisioner.h
    src/application/accounts/IProfileResetter.h
    src/application/security/PasswordTaskService.cpp
    src/application/security/SecurePasswordBuffer.cpp
    src/domain/account/Account.cpp
    src/domain/account/AccountId.cpp
    src/domain/account/PasswordVerifier.cpp
    src/domain/account/Username.cpp
    src/infrastructure/account/SqlAccountRepository.cpp
    src/infrastructure/account/SodiumPasswordHasher.cpp
    src/infrastructure/filesystem/LocalUserDirectoryProvisioner.cpp
    src/infrastructure/filesystem/LocalProfileResetter.cpp
)
target_include_directories(devhub_accounts PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(devhub_accounts
    PUBLIC project_options devhub_core devhub_database Qt6::Core Qt6::Sql Qt6::Concurrent
           unofficial-sodium::sodium
    PRIVATE project_warnings
)

add_library(devhub_workspaces STATIC
    src/application/workspaces/IWorkspaceRepository.h
    src/application/workspaces/WorkspaceContext.cpp
    src/application/workspaces/WorkspaceManager.cpp
    src/application/workspaces/WorkspaceRelocationService.cpp
    src/application/workspaces/WorkspaceStateStore.cpp
    src/application/workspaces/WorkspaceValidation.cpp
    src/domain/workspace/WorkspaceId.cpp
    src/infrastructure/workspace/SqlWorkspaceRepository.cpp
    src/infrastructure/workspace/SystemTerminalLauncher.cpp
)
target_include_directories(devhub_workspaces PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(devhub_workspaces PUBLIC project_options devhub_accounts Qt6::Core Qt6::Sql
    PRIVATE project_warnings)

add_library(devhub_detection STATIC
    src/application/detection/WorkspaceDetectorRegistry.cpp
    src/application/detection/WorkspaceScanService.cpp
    src/application/detection/WorkspaceScanner.cpp
    src/infrastructure/detection/SqlDetectionResultRepository.cpp
)
target_include_directories(devhub_detection PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(devhub_detection PUBLIC project_options devhub_workspaces devhub_core devhub_database
    Qt6::Core Qt6::Sql PRIVATE project_warnings)

add_library(devhub_services STATIC
    src/application/services/ServiceDefinitionValidator.cpp
    src/application/services/ManagedProcess.cpp
    src/application/services/ServiceCoordinator.cpp
    src/infrastructure/services/SqlServiceDefinitionRepository.cpp
    src/presentation/services/ServiceListModel.cpp
)
target_include_directories(devhub_services PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(devhub_services PUBLIC project_options devhub_workspaces devhub_core devhub_database Qt6::Core Qt6::Sql Qt6::Network
    PRIVATE project_warnings)

add_library(devhub_ports STATIC
    src/application/ports/PortMonitorService.cpp
    src/infrastructure/ports/WindowsPortProvider.cpp
    src/infrastructure/ports/WindowsProcessInspector.cpp
)
target_include_directories(devhub_ports PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(devhub_ports PUBLIC project_options devhub_core Qt6::Core Qt6::Network Qt6::Concurrent
    PRIVATE project_warnings)
if(WIN32)
    target_link_libraries(devhub_ports PRIVATE iphlpapi ws2_32)
endif()

add_library(devhub_logs STATIC
    src/application/logs/LogDecoder.cpp
    src/application/logs/LogParser.cpp
    src/application/logs/LogRingBuffer.cpp
    src/infrastructure/logs/FileIdentity.cpp
    src/infrastructure/logs/FileLogReader.cpp
    src/infrastructure/logs/SqlLogSourceRepository.cpp
)
target_include_directories(devhub_logs PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(devhub_logs PUBLIC project_options devhub_database Qt6::Core Qt6::Sql
    PRIVATE project_warnings)

add_executable(devhub WIN32
    src/application/logging/ApplicationLogCategories.cpp
    src/application/settings/SettingsService.cpp
    src/application/support/SupportBundleBuilder.cpp
    src/application/overview/OverviewStateComposer.cpp
    src/application/commands/CommandRegistry.cpp
    src/application/commands/ICommandSearchProvider.cpp
    src/app/ApplicationBootstrapper.cpp
    src/app/RecoveryModeWindow.cpp
    src/app/main.cpp
    src/app/RuntimeCheck.cpp
    src/presentation/theme/ThemeManager.cpp
    src/presentation/theme/ThemePalette.cpp
    src/presentation/theme/ThemeProxyStyle.cpp
    src/presentation/theme/ThemeTokens.cpp
    src/presentation/cursor/CustomCursorController.cpp
    src/presentation/icons/IconLoader.cpp
    src/presentation/widgets/ReusableWidgets.cpp
    src/presentation/widgets/SafeMarkdownBrowser.cpp
    src/presentation/assistant/AssistantPage.cpp
    src/presentation/shell/MainWindow.cpp
    src/presentation/overview/OverviewPage.cpp
    src/presentation/commands/CommandPaletteDialog.cpp
    src/presentation/settings/SettingsWindow.cpp
    src/presentation/settings/AssistantSettingsPanel.cpp
    src/presentation/settings/SupportBundleDialog.cpp
    src/presentation/notifications/NotificationListModel.cpp
    src/presentation/notifications/NotificationCenterDialog.cpp
    src/presentation/notifications/ToastManager.cpp
    src/presentation/git/GitPage.cpp
    src/presentation/services/ServicePage.cpp
    src/presentation/ports/PortPage.cpp
    src/presentation/ports/PortTableModel.cpp
    src/presentation/logs/LogsPage.cpp
    src/presentation/logs/LogLineModel.cpp
    src/presentation/databases/DatabasePage.cpp
    src/presentation/databases/DatabaseTableModel.cpp
    src/presentation/notes/NotesPage.cpp
    src/presentation/files/FileBrowserController.cpp
    src/presentation/files/FileBrowserPage.cpp
    src/infrastructure/notes/NotesRepository.cpp
    src/application/notes/NoteFolderExporter.cpp
    src/infrastructure/files/WindowsTrashService.cpp
    src/infrastructure/files/QProcessExternalEditorLauncher.cpp
    src/infrastructure/settings/SqlSettingsRepository.cpp
    src/infrastructure/notifications/SqlNotificationRepository.cpp
    src/infrastructure/logging/ApplicationLogManager.cpp
    src/infrastructure/workspace/WindowsTerminalLauncher.cpp
    src/application/git/GitStateStore.cpp
    src/presentation/workspaces/AddWorkspaceDialog.cpp
    src/presentation/workspaces/WorkspaceListDelegate.cpp
    src/presentation/workspaces/WorkspaceListModel.cpp
    src/presentation/workspaces/WorkspaceListView.cpp
    src/presentation/authentication/AuthenticationWidgets.cpp
    src/presentation/authentication/AuthenticationWindow.cpp
    src/presentation/authentication/ChangePasswordDialog.cpp
    resources/cursors.qrc
    resources/devhub.qrc
    resources/database_migrations.qrc
)
if(WIN32)
    target_sources(devhub PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/version.rc")
endif()
target_link_libraries(devhub PRIVATE
    project_options
    project_warnings
    devhub_core
    devhub_assistant
    devhub_database
    devhub_accounts
    devhub_workspaces
    devhub_detection
    devhub_services
    devhub_ports
    devhub_logs
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Svg
    Qt6::Concurrent
)
set_source_files_properties(
    resources/qml/Authentication.qml PROPERTIES QT_RESOURCE_ALIAS Authentication.qml)
set_source_files_properties(resources/qml/MainShell.qml PROPERTIES QT_RESOURCE_ALIAS MainShell.qml)
set_source_files_properties(resources/qml/Theme.qml PROPERTIES
    QT_RESOURCE_ALIAS Theme.qml QT_QML_SINGLETON_TYPE TRUE)
set_source_files_properties(resources/qml/BrandMark.qml PROPERTIES QT_RESOURCE_ALIAS BrandMark.qml)
set_source_files_properties(resources/qml/DarkField.qml PROPERTIES QT_RESOURCE_ALIAS DarkField.qml)
set_source_files_properties(resources/qml/PinkButton.qml PROPERTIES QT_RESOURCE_ALIAS PinkButton.qml)
set_source_files_properties(resources/qml/PasswordField.qml PROPERTIES QT_RESOURCE_ALIAS PasswordField.qml)
set_source_files_properties(resources/qml/LockMark.qml PROPERTIES QT_RESOURCE_ALIAS LockMark.qml)
set_source_files_properties(resources/qml/ShellButton.qml PROPERTIES QT_RESOURCE_ALIAS ShellButton.qml)
qt_add_qml_module(devhub
    URI DevHub
    VERSION 1.0
    DEPENDENCIES QtQuick.Controls QtQuick.Layouts
    QML_FILES
        resources/qml/Authentication.qml
        resources/qml/MainShell.qml
        resources/qml/Theme.qml
        resources/qml/BrandMark.qml
        resources/qml/DarkField.qml
        resources/qml/PinkButton.qml
        resources/qml/PasswordField.qml
        resources/qml/LockMark.qml
        resources/qml/ShellButton.qml
)
qt_import_qml_plugins(devhub)

# Qt's generated QML cache code can instantiate unreachable branches in Qt
# headers under MSVC /O2. Keep /W4 and /WX for application sources and suppress
# only C4702 in the generated cache compilation units.
get_target_property(_devhub_sources devhub SOURCES)
foreach(_devhub_source IN LISTS _devhub_sources)
    if(_devhub_source MATCHES "/\\.rcc/qmlcache/.*\\.cpp$")
        set_property(SOURCE "${_devhub_source}" APPEND PROPERTY COMPILE_OPTIONS
            "$<$<CXX_COMPILER_ID:MSVC>:/wd4702>")
    endif()
endforeach()

if(WIN32)
    target_link_libraries(devhub PRIVATE shell32)
endif()
if(MSVC)
    # The version resource already embeds our custom asInvoker manifest.
    target_link_options(devhub PRIVATE /MANIFEST:NO)
endif()

if(DEVHUB_BUILD_TESTS)
    find_package(Qt6 6.11.0 REQUIRED COMPONENTS Test)
    add_subdirectory(tests)
endif()
if(DEVHUB_BUILD_LIVE_ASSISTANT_TESTS AND NOT DEVHUB_BUILD_TESTS)
    message(FATAL_ERROR "DEVHUB_BUILD_LIVE_ASSISTANT_TESTS requires DEVHUB_BUILD_TESTS=ON")
endif()

if(DEVHUB_ENABLE_PACKAGING)
    include(cmake/Packaging.cmake)
endif()
