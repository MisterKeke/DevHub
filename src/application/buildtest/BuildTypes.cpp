#include "BuildTypes.h"

namespace devhub::application::buildtest {

QString runnerName(RunnerType runner)
{
    switch (runner) {
    case RunnerType::CMakeConfigure: return QStringLiteral("CMake configure");
    case RunnerType::CMakeBuild: return QStringLiteral("CMake build");
    case RunnerType::CTest: return QStringLiteral("CTest");
    case RunnerType::GoBuild: return QStringLiteral("Go build");
    case RunnerType::GoTest: return QStringLiteral("Go test");
    case RunnerType::Pytest: return QStringLiteral("pytest");
    case RunnerType::NpmScript: return QStringLiteral("npm script");
    case RunnerType::MakeTarget: return QStringLiteral("Make target");
    case RunnerType::Custom: return QStringLiteral("Custom command");
    }
    return QStringLiteral("Unknown");
}

QString runStatusName(RunStatus status)
{
    switch (status) {
    case RunStatus::Running: return QStringLiteral("Running");
    case RunStatus::Succeeded: return QStringLiteral("Succeeded");
    case RunStatus::Failed: return QStringLiteral("Failed");
    case RunStatus::Cancelled: return QStringLiteral("Cancelled");
    case RunStatus::TimedOut: return QStringLiteral("Timed out");
    case RunStatus::StartFailed: return QStringLiteral("Could not start");
    }
    return QStringLiteral("Unknown");
}

bool isTestRunner(RunnerType runner)
{
    return runner == RunnerType::CTest || runner == RunnerType::GoTest || runner == RunnerType::Pytest;
}

} // namespace devhub::application::buildtest
