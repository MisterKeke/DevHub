cmake_minimum_required(VERSION 3.28)

project(DevHub VERSION 0.0.1 LANGUAGES CXX)

include(CTest)
include(cmake/ProjectOptions.cmake)
include(cmake/ProjectWarnings.cmake)

option(DEVHUB_BUILD_TESTS "Build DevHub tests" ${BUILD_TESTING})
option(DEVHUB_ENABLE_PACKAGING "Enable installer and packaging targets" OFF)
option(DEVHUB_ENABLE_SANITIZERS "Enable supported compiler sanitizers" OFF)
option(DEVHUB_WARNINGS_AS_ERRORS "Treat DevHub warnings as errors" OFF)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

find_package(Qt6 6.8.3 REQUIRED COMPONENTS Core Gui Widgets Sql Concurrent Svg Network)
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

add_library(devhub_database STATIC
    src/application/databases/DatabaseTypes.cpp
    src/application/databases/IDatabaseSession.h
    src/infrastructure/database/SqlDatabaseReferenceRepository.cpp
    src/infrastructure/database/QtSqlConnectionFactory.cpp
    src/infrastructure/database/QtSqlDatabaseSession.cpp
    src/infrastructure/database/QtSqlDatabaseWorker.cpp
    src/infrastructure/database/QtSqlDatabaseWorker.h
    src/infrastructure/database/DatabaseConnectionName.cpp
    src/infrastructure/database/InternalDatabaseManager.cpp
    src/infrastructure/database/SqlErrorMapper.cpp
    src/infrastructure/database/SqlTransactionGuard.cpp
)
target_include_directories(devhub_database PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")
target_link_libraries(devhub_database
    PUBLIC project_options devhub_core Qt6::Core Qt6::Sql
    PRIVATE project_warnings
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
    src/presentation/theme/ThemeManager.cpp
    src/presentation/theme/ThemePalette.cpp
    src/presentation/theme/ThemeProxyStyle.cpp
    src/presentation/theme/ThemeTokens.cpp
    src/presentation/icons/IconLoader.cpp
    src/presentation/widgets/ReusableWidgets.cpp
    src/presentation/shell/MainWindow.cpp
    src/presentation/overview/OverviewPage.cpp
    src/presentation/commands/CommandPaletteDialog.cpp
    src/presentation/settings/SettingsWindow.cpp
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
    src/infrastructure/files/WindowsTrashService.cpp
    src/infrastructure/files/QProcessExternalEditorLauncher.cpp
    src/infrastructure/settings/SqlSettingsRepository.cpp
    src/infrastructure/notifications/SqlNotificationRepository.cpp
    src/infrastructure/logging/ApplicationLogManager.cpp
    src/infrastructure/workspace/WindowsTerminalLauncher.cpp
    src/application/git/GitExecutableLocator.cpp
    src/application/git/GitCommandRunner.cpp
    src/application/git/GitStatusParser.cpp
    src/application/git/GitRepositoryLocator.cpp
    src/application/git/GitService.cpp
    src/application/git/GitStateStore.cpp
    src/presentation/workspaces/AddWorkspaceDialog.cpp
    src/presentation/workspaces/WorkspaceListDelegate.cpp
    src/presentation/workspaces/WorkspaceListModel.cpp
    src/presentation/workspaces/WorkspaceListView.cpp
    src/presentation/authentication/AuthenticationWidgets.cpp
    src/presentation/authentication/AuthenticationWindow.cpp
    src/presentation/authentication/ForgotPasswordInformationDialog.cpp
    src/presentation/authentication/ChangePasswordDialog.cpp
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
    Qt6::Svg
    Qt6::Concurrent
)
if(WIN32)
    target_link_libraries(devhub PRIVATE shell32)
endif()

if(DEVHUB_BUILD_TESTS)
    find_package(Qt6 6.8.3 REQUIRED COMPONENTS Test)
    add_subdirectory(tests)
endif()

if(DEVHUB_ENABLE_PACKAGING)
    include(cmake/Packaging.cmake)
endif()
