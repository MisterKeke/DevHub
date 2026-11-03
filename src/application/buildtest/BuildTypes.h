#pragma once

#include <QDateTime>
#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

namespace devhub::application::buildtest {

enum class RunnerType { CMakeConfigure, CMakeBuild, CTest, GoBuild, GoTest, Pytest, NpmScript, MakeTarget, Custom };
enum class RunStatus { Running, Succeeded, Failed, Cancelled, TimedOut, StartFailed };

struct BuildProfile final {
    QString id;
    QString workspaceId;
    QString name;
    RunnerType runner = RunnerType::Custom;
    QString workingDirectory;
    QString preset;
    QString configuration;
    QString buildDirectory;
    QString target;
    QString testFilter;
    QString program;
    QStringList arguments;
    int timeoutSeconds = 1800;
    bool trusted = false;
    bool testAction = false;
};

struct DetectedProject final {
    QString workspaceId;
    QString rootPath;
    QStringList buildSystems;
    QStringList configurePresets;
    QStringList buildPresets;
    QStringList testPresets;
    QHash<QString, QString> presetBinaryDirectories;
    QHash<QString, QString> buildPresetBinaryDirectories;
    QHash<QString, QString> testPresetBinaryDirectories;
    QStringList nodeScripts;
    QStringList warnings;
    bool hasGoModule = false;
    bool hasGoWorkspace = false;
    bool hasPytest = false;
    bool hasCMake = false;
    bool hasMake = false;
    bool hasNode = false;
};

struct TestResult final {
    QString id;
    QString runId;
    QString packageName;
    QString name;
    QString status;
    qint64 durationMilliseconds = 0;
    QString details;
};

struct Diagnostic final {
    QString filePath;
    int line = 0;
    int column = 0;
    QString severity;
    QString message;
    QString stage;
};

struct BuildRun final {
    QString id;
    QString workspaceId;
    QString profileId;
    QString profileName;
    RunnerType runner = RunnerType::Custom;
    RunStatus status = RunStatus::Running;
    QDateTime startedAtUtc;
    QDateTime finishedAtUtc;
    qint64 durationMilliseconds = 0;
    int exitCode = -1;
    QString summary;
    QString logReference;
    QString taskId;
    bool testAction = false;
    int totalTests = 0;
    int passedTests = 0;
    int failedTests = 0;
    int skippedTests = 0;
    QVector<TestResult> tests;
    QVector<Diagnostic> diagnostics;
};

[[nodiscard]] QString runnerName(RunnerType runner);
[[nodiscard]] QString runStatusName(RunStatus status);
[[nodiscard]] bool isTestRunner(RunnerType runner);

} // namespace devhub::application::buildtest

