#include "BuildSystemDetector.h"

#include "../../domain/errors/AppError.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSet>
#include <QUuid>

namespace devhub::application::buildtest {
namespace {

QStringList presetNames(const QJsonObject& root, const QString& key)
{
    QStringList names;
    for (const QJsonValue& value : root.value(key).toArray()) {
        if (!value.isObject()) continue;
        const QJsonObject preset = value.toObject();
        if (preset.value(QStringLiteral("hidden")).toBool()) continue;
        const QString name = preset.value(QStringLiteral("name")).toString().trimmed();
        if (!name.isEmpty()) names.append(name);
    }
    return names;
}

QString inheritedString(const QJsonArray& presets, const QString& name, const QString& key,
                        QSet<QString>& visited)
{
    if (name.isEmpty() || visited.contains(name)) return {};
    visited.insert(name);
    for (const QJsonValue& value : presets) {
        if (!value.isObject()) continue;
        const QJsonObject preset = value.toObject();
        if (preset.value(QStringLiteral("name")).toString() != name) continue;
        const QString own = preset.value(key).toString();
        if (!own.isEmpty()) return own;
        QJsonArray parents;
        const QJsonValue inherits = preset.value(QStringLiteral("inherits"));
        if (inherits.isString()) parents.append(inherits);
        else if (inherits.isArray()) parents = inherits.toArray();
        for (const QJsonValue& parent : parents) {
            const QString inheritedValue = inheritedString(presets, parent.toString(), key, visited);
            if (!inheritedValue.isEmpty()) return inheritedValue;
        }
        return {};
    }
    return {};
}

QJsonObject readJsonObject(const QString& path, QString* error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error) *error = file.errorString();
        return {};
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.read(4 * 1024 * 1024), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        if (error) *error = parseError.errorString();
        return {};
    }
    return document.object();
}

QString expandBinaryDirectory(QString value, const QString& root, const QString& sourceName,
                               const QString& presetName, const QString& generator)
{
    if (value.contains(QStringLiteral("$env{")) || value.contains(QStringLiteral("$penv{")))
        return {};
    if (value.contains(QStringLiteral("${generator}")) && generator.isEmpty()) return {};
    value.replace(QStringLiteral("${sourceDir}"), QDir::toNativeSeparators(root));
    value.replace(QStringLiteral("${sourceDirName}"), sourceName);
    value.replace(QStringLiteral("${presetName}"), presetName);
    value.replace(QStringLiteral("${generator}"), generator);
    static const QRegularExpression unresolvedMacro(QStringLiteral(R"((\$\{[^}]+\}|\$p?env\{[^}]+\}))"));
    if (unresolvedMacro.match(value).hasMatch()) return {};
    return QDir::cleanPath(QFileInfo(value).isAbsolute() ? value : QDir(root).filePath(value));
}

QString stableId(const QString& workspaceId, RunnerType runner, const QString& profileKey)
{
    const QByteArray name = QStringLiteral("buildtest:%1:%2:%3")
        .arg(workspaceId).arg(static_cast<int>(runner)).arg(profileKey).toUtf8();
    return QUuid::createUuidV5(QUuid(QStringLiteral("{abf5c802-54a8-5d2a-88c6-ea0441b75062}")), name)
        .toString(QUuid::WithoutBraces);
}

BuildProfile baseProfile(const DetectedProject& project, RunnerType runner,
                         const QString& name, const QString& key)
{
    BuildProfile profile;
    profile.id = stableId(project.workspaceId, runner, key);
    profile.workspaceId = project.workspaceId;
    profile.name = name;
    profile.runner = runner;
    profile.workingDirectory = project.rootPath;
    return profile;
}

bool containsPytestConfig(const QString& root)
{
    if (QFileInfo::exists(QDir(root).filePath(QStringLiteral("pytest.ini")))) return true;
    for (const QString& name : {QStringLiteral("pyproject.toml"), QStringLiteral("setup.cfg"), QStringLiteral("tox.ini")}) {
        QFile file(QDir(root).filePath(name));
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) continue;
        const QByteArray content = file.read(1024 * 1024);
        if (content.contains("[tool.pytest.ini_options]") || content.contains("[pytest]")
            || content.contains("[tool:pytest]")) return true;
    }
    return false;
}

} // namespace

shared::common::Result<DetectedProject> BuildSystemDetector::detect(const QString& workspaceId,
                                                                     const QString& workspaceRoot) const
{
    const QFileInfo rootInfo(workspaceRoot);
    const QString root = rootInfo.canonicalFilePath();
    if (workspaceId.trimmed().isEmpty() || root.isEmpty() || !rootInfo.isDir()) {
        return shared::common::Result<DetectedProject>::failure(domain::errors::AppError::failure(
            domain::errors::ErrorCode::InvalidArgument, domain::errors::ErrorCategory::Workspace,
            QStringLiteral("The active workspace folder is unavailable."), workspaceRoot,
            QStringLiteral("detect build systems")));
    }

    DetectedProject project;
    project.workspaceId = workspaceId;
    project.rootPath = root;
    if (QFileInfo::exists(QDir(root).filePath(QStringLiteral("CMakeLists.txt")))) {
        project.hasCMake = true;
        project.buildSystems.append(QStringLiteral("CMake"));
        QJsonArray inheritedConfigurePresets;
        QJsonArray inheritedBuildPresets;
        QJsonArray inheritedTestPresets;
        for (const QString& name : {QStringLiteral("CMakePresets.json"), QStringLiteral("CMakeUserPresets.json")}) {
            const QString path = QDir(root).filePath(name);
            if (!QFileInfo::exists(path)) continue;
            QString error;
            const QJsonObject json = readJsonObject(path, &error);
            if (json.isEmpty()) {
                project.warnings.append(QStringLiteral("Could not read %1: %2").arg(name, error));
                continue;
            }
            const QStringList configNames = presetNames(json, QStringLiteral("configurePresets"));
            const QStringList buildNames = presetNames(json, QStringLiteral("buildPresets"));
            const QStringList testNames = presetNames(json, QStringLiteral("testPresets"));
            for (const QString& value : configNames) if (!project.configurePresets.contains(value)) project.configurePresets.append(value);
            for (const QString& value : buildNames) if (!project.buildPresets.contains(value)) project.buildPresets.append(value);
            for (const QString& value : testNames) if (!project.testPresets.contains(value)) project.testPresets.append(value);
            const QJsonArray configurePresets = json.value(QStringLiteral("configurePresets")).toArray();
            const QJsonArray buildPresets = json.value(QStringLiteral("buildPresets")).toArray();
            const QJsonArray testPresets = json.value(QStringLiteral("testPresets")).toArray();
            QJsonArray availableConfigurePresets = inheritedConfigurePresets;
            for (const QJsonValue& configurePreset : configurePresets) availableConfigurePresets.append(configurePreset);
            for (const QJsonValue& value : configurePresets) {
                if (!value.isObject()) continue;
                const QJsonObject preset = value.toObject();
                if (preset.value(QStringLiteral("hidden")).toBool()) continue;
                const QString configureName = preset.value(QStringLiteral("name")).toString();
                QSet<QString> visited;
                const QString binaryDir = inheritedString(availableConfigurePresets, configureName, QStringLiteral("binaryDir"), visited);
                visited.clear();
                const QString generator = inheritedString(availableConfigurePresets, configureName, QStringLiteral("generator"), visited);
                if (!configureName.isEmpty() && !binaryDir.isEmpty() && !project.presetBinaryDirectories.contains(configureName)) {
                    const QString resolvedDirectory = expandBinaryDirectory(
                        binaryDir, root, QFileInfo(root).fileName(), configureName, generator);
                    if (!resolvedDirectory.isEmpty())
                        project.presetBinaryDirectories.insert(configureName, resolvedDirectory);
                }
            }
            for (const QJsonValue& configurePreset : configurePresets) inheritedConfigurePresets.append(configurePreset);

            QJsonArray availableBuildPresets = inheritedBuildPresets;
            for (const QJsonValue& buildPreset : buildPresets) availableBuildPresets.append(buildPreset);
            for (const QJsonValue& buildPresetValue : buildPresets) {
                if (!buildPresetValue.isObject() || buildPresetValue.toObject().value(QStringLiteral("hidden")).toBool()) continue;
                const QString buildName = buildPresetValue.toObject().value(QStringLiteral("name")).toString();
                QSet<QString> visited;
                const QString configureName = inheritedString(availableBuildPresets, buildName,
                    QStringLiteral("configurePreset"), visited);
                const QString binaryDirectory = project.presetBinaryDirectories.value(configureName);
                if (!buildName.isEmpty() && !binaryDirectory.isEmpty())
                    project.buildPresetBinaryDirectories.insert(buildName, binaryDirectory);
            }
            for (const QJsonValue& buildPreset : buildPresets) inheritedBuildPresets.append(buildPreset);

            QJsonArray availableTestPresets = inheritedTestPresets;
            for (const QJsonValue& testPreset : testPresets) availableTestPresets.append(testPreset);
            for (const QJsonValue& testPresetValue : testPresets) {
                if (!testPresetValue.isObject() || testPresetValue.toObject().value(QStringLiteral("hidden")).toBool()) continue;
                const QString testName = testPresetValue.toObject().value(QStringLiteral("name")).toString();
                QSet<QString> visited;
                const QString configureName = inheritedString(availableTestPresets, testName,
                    QStringLiteral("configurePreset"), visited);
                const QString binaryDirectory = project.presetBinaryDirectories.value(configureName);
                if (!testName.isEmpty() && !binaryDirectory.isEmpty())
                    project.testPresetBinaryDirectories.insert(testName, binaryDirectory);
            }
            for (const QJsonValue& testPreset : testPresets) inheritedTestPresets.append(testPreset);
        }
        for (const QString& preset : project.configurePresets)
            if (!project.presetBinaryDirectories.contains(preset))
                project.warnings.append(QStringLiteral("Preset '%1' has a build directory DevHub cannot safely resolve; related runs are unavailable.").arg(preset));
    }
    project.hasGoModule = QFileInfo::exists(QDir(root).filePath(QStringLiteral("go.mod")));
    project.hasGoWorkspace = QFileInfo::exists(QDir(root).filePath(QStringLiteral("go.work")));
    if (project.hasGoModule || project.hasGoWorkspace) project.buildSystems.append(QStringLiteral("Go"));
    project.hasPytest = containsPytestConfig(root);
    if (project.hasPytest) project.buildSystems.append(QStringLiteral("Python / pytest"));
    const QString packagePath = QDir(root).filePath(QStringLiteral("package.json"));
    if (QFileInfo::exists(packagePath)) {
        project.hasNode = true;
        project.buildSystems.append(QStringLiteral("Node.js"));
        QString error;
        const QJsonObject package = readJsonObject(packagePath, &error);
        if (package.isEmpty()) project.warnings.append(QStringLiteral("Could not read package.json: %1").arg(error));
        else {
            const QJsonObject scripts = package.value(QStringLiteral("scripts")).toObject();
            project.nodeScripts = scripts.keys();
            std::sort(project.nodeScripts.begin(), project.nodeScripts.end());
        }
    }
    project.hasMake = QFileInfo::exists(QDir(root).filePath(QStringLiteral("Makefile")))
        || QFileInfo::exists(QDir(root).filePath(QStringLiteral("makefile")));
    if (project.hasMake) project.buildSystems.append(QStringLiteral("Make"));
    return shared::common::Result<DetectedProject>::success(std::move(project));
}

QVector<BuildProfile> BuildSystemDetector::defaultProfiles(const DetectedProject& project) const
{
    QVector<BuildProfile> profiles;
    if (project.hasCMake) {
        for (const QString& preset : project.configurePresets) {
            if (!project.presetBinaryDirectories.contains(preset)) continue;
            auto profile = baseProfile(project, RunnerType::CMakeConfigure,
                QStringLiteral("Configure · %1").arg(preset), QStringLiteral("configure:%1").arg(preset));
            profile.preset = preset;
            profile.buildDirectory = project.presetBinaryDirectories.value(preset);
            profiles.append(std::move(profile));
        }
        if (project.configurePresets.isEmpty()) {
            auto profile = baseProfile(project, RunnerType::CMakeConfigure,
                QStringLiteral("CMake Configure"), QStringLiteral("configure:default"));
            profile.buildDirectory = QDir(project.rootPath).filePath(QStringLiteral("build"));
            profiles.append(std::move(profile));
        }
        for (const QString& preset : project.buildPresets) {
            if (!project.buildPresetBinaryDirectories.contains(preset)) continue;
            auto profile = baseProfile(project, RunnerType::CMakeBuild,
                QStringLiteral("Build · %1").arg(preset), QStringLiteral("build:%1").arg(preset));
            profile.preset = preset;
            profile.buildDirectory = project.buildPresetBinaryDirectories.value(preset);
            profiles.append(std::move(profile));
        }
        if (project.buildPresets.isEmpty()) {
            QStringList buildDirectories;
            if (project.configurePresets.isEmpty())
                buildDirectories.append(QDir(project.rootPath).filePath(QStringLiteral("build")));
            else for (const QString& preset : project.configurePresets) {
                const QString directory = project.presetBinaryDirectories.value(preset);
                if (!directory.isEmpty() && !buildDirectories.contains(directory)) buildDirectories.append(directory);
            }
            for (const QString& directory : buildDirectories) {
                const QString suffix = project.configurePresets.isEmpty() ? QString{} : QStringLiteral(" · %1").arg(
                    QDir(project.rootPath).relativeFilePath(directory));
                for (const QString& configuration : {QStringLiteral("Debug"), QStringLiteral("Release")}) {
                    auto profile = baseProfile(project, RunnerType::CMakeBuild,
                        QStringLiteral("CMake Build · %1%2").arg(configuration, suffix),
                        QStringLiteral("build:%1:%2").arg(configuration.toLower(), directory));
                    profile.configuration = configuration;
                    profile.buildDirectory = directory;
                    profiles.append(std::move(profile));
                }
            }
        }
        for (const QString& preset : project.testPresets) {
            if (!project.testPresetBinaryDirectories.contains(preset)) continue;
            auto profile = baseProfile(project, RunnerType::CTest,
                QStringLiteral("Test · %1").arg(preset), QStringLiteral("test:%1").arg(preset));
            profile.preset = preset;
            profile.buildDirectory = project.testPresetBinaryDirectories.value(preset);
            profile.testAction = true;
            profiles.append(std::move(profile));
        }
        if (project.testPresets.isEmpty()) {
            QStringList testDirectories;
            if (project.configurePresets.isEmpty())
                testDirectories.append(QDir(project.rootPath).filePath(QStringLiteral("build")));
            else for (const QString& preset : project.configurePresets) {
                const QString directory = project.presetBinaryDirectories.value(preset);
                if (!directory.isEmpty() && !testDirectories.contains(directory)) testDirectories.append(directory);
            }
            for (const QString& directory : testDirectories) {
                const QString suffix = project.configurePresets.isEmpty() ? QString{} : QStringLiteral(" · %1").arg(
                    QDir(project.rootPath).relativeFilePath(directory));
                for (const QString& configuration : {QStringLiteral("Debug"), QStringLiteral("Release")}) {
                    auto profile = baseProfile(project, RunnerType::CTest,
                        QStringLiteral("CTest · %1%2").arg(configuration, suffix),
                        QStringLiteral("test:%1:%2").arg(configuration.toLower(), directory));
                    profile.configuration = configuration;
                    profile.testAction = true;
                    profile.buildDirectory = directory;
                    profiles.append(std::move(profile));
                }
            }
        }
    }
    if (project.hasGoModule || project.hasGoWorkspace) {
        profiles.append(baseProfile(project, RunnerType::GoBuild, QStringLiteral("Go Build"), QStringLiteral("go:build")));
        auto tests = baseProfile(project, RunnerType::GoTest, QStringLiteral("Go Tests"), QStringLiteral("go:test"));
        tests.testAction = true;
        profiles.append(std::move(tests));
    }
    if (project.hasPytest) {
        auto tests = baseProfile(project, RunnerType::Pytest,
            QStringLiteral("Python Tests (pytest)"), QStringLiteral("python:pytest"));
        tests.testAction = true;
        profiles.append(std::move(tests));
    }
    return profiles;
}

} // namespace devhub::application::buildtest
