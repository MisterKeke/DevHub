#include "WorkspaceDetectorRegistry.h"

#include <QFile>
#include <QRegularExpression>
#include <algorithm>

namespace devhub::application::detection {
namespace {
QString normalizedName(const QString& name) { return name.toCaseFolded(); }
QString combinedPath(const QString& directory, const QString& name)
{
    return directory == QStringLiteral(".") ? name : directory + QLatin1Char('/') + name;
}

class MarkerDetector final : public IWorkspaceDetector
{
public:
    MarkerDetector(QString key, int priority, QStringList names, QString description,
                   ConfidenceLevel confidence, bool prefix = false, bool extension = false,
                   bool directoryOnly = false, bool sqliteHeader = false)
        : m_id(std::move(key)), m_priority(priority), m_names(std::move(names)),
          m_description(std::move(description)), m_confidence(confidence), m_prefix(prefix),
          m_extension(extension), m_directoryOnly(directoryOnly), m_sqliteHeader(sqliteHeader) {}

    DetectorId id() const override { return DetectorId(m_id); }
    int priority() const noexcept override { return m_priority; }
    int version() const noexcept override { return 1; }

    QVector<DetectionEvidence> inspect(const DetectionEntry& entry, const ScanBudget& budget) const override
    {
        const QString name = normalizedName(entry.name);
        bool matches = false;
        for (const QString& candidate : m_names) {
            if (m_prefix ? name.startsWith(candidate) : (m_extension ? name.endsWith(candidate) : name == candidate)) {
                matches = true; break;
            }
        }
        if (!matches || (m_directoryOnly && !entry.isDirectory)) return {};
        if (m_sqliteHeader && budget.validateSQLiteHeader) {
            QFile file(entry.info.absoluteFilePath());
            if (!file.open(QIODevice::ReadOnly) || file.read(16) != QByteArray("SQLite format 3\0", 16)) return {};
        }
        const QString relativePath = combinedPath(entry.relativeDirectory, entry.name);
        return {{DetectorId(m_id), relativePath, entry.relativeDirectory, m_description, m_confidence, version()}};
    }

private:
    QString m_id;
    int m_priority;
    QStringList m_names;
    QString m_description;
    ConfidenceLevel m_confidence;
    bool m_prefix;
    bool m_extension;
    bool m_directoryOnly;
    bool m_sqliteHeader;
};

class CMakeBuildDirectoryDetector final : public IWorkspaceDetector
{
public:
    DetectorId id() const override { return DetectorId(QStringLiteral("cmake-build-directory")); }
    int priority() const noexcept override { return 5; }
    int version() const noexcept override { return 1; }
    QVector<DetectionEvidence> inspect(const DetectionEntry& entry, const ScanBudget&) const override
    {
        if (!entry.isDirectory) return {};
        static const QRegularExpression pattern(QStringLiteral("^(?:build(?:[-_].*)?|cmake-build(?:-.*)?|out-build.*)$"),
                                                 QRegularExpression::CaseInsensitiveOption);
        if (!pattern.match(entry.name).hasMatch()) return {};
        const QString relative = entry.relativeDirectory == QStringLiteral(".")
            ? entry.name : entry.relativeDirectory + QLatin1Char('/') + entry.name;
        return {{id(), relative, entry.relativeDirectory, QStringLiteral("CMake build folder"), ConfidenceLevel::Low, version()}};
    }
};

std::shared_ptr<const IWorkspaceDetector> marker(QString id, int priority, QStringList names,
                                                 QString description, ConfidenceLevel confidence,
                                                 bool prefix = false, bool extension = false,
                                                 bool directoryOnly = false, bool sqlite = false)
{
    return std::make_shared<MarkerDetector>(std::move(id), priority, std::move(names),
        std::move(description), confidence, prefix, extension, directoryOnly, sqlite);
}
}

WorkspaceDetectorRegistry::WorkspaceDetectorRegistry()
{
    registerDetector(marker(QStringLiteral("git"), 10, {QStringLiteral(".git")}, QStringLiteral("Git repository marker"), ConfidenceLevel::High));
    registerDetector(std::make_shared<CMakeBuildDirectoryDetector>());
    registerDetector(marker(QStringLiteral("cmake"), 20, {QStringLiteral("cmakelists.txt")}, QStringLiteral("CMake project"), ConfidenceLevel::High));
    registerDetector(marker(QStringLiteral("npm"), 30, {QStringLiteral("package.json")}, QStringLiteral("Node.js package manifest"), ConfidenceLevel::High));
    registerDetector(marker(QStringLiteral("go"), 40, {QStringLiteral("go.mod")}, QStringLiteral("Go module"), ConfidenceLevel::High));
    registerDetector(marker(QStringLiteral("cargo"), 50, {QStringLiteral("cargo.toml")}, QStringLiteral("Rust Cargo manifest"), ConfidenceLevel::High));
    registerDetector(marker(QStringLiteral("visual-studio"), 60,
        {QStringLiteral(".sln"), QStringLiteral(".slnx"), QStringLiteral(".csproj"), QStringLiteral(".vcxproj"), QStringLiteral(".fsproj")},
        QStringLiteral("Visual Studio solution or project"), ConfidenceLevel::High, false, true));
    registerDetector(marker(QStringLiteral("dockerfile"), 70, {QStringLiteral("dockerfile")}, QStringLiteral("Dockerfile"), ConfidenceLevel::High, true));
    registerDetector(marker(QStringLiteral("compose"), 80,
        {QStringLiteral("compose.yaml"), QStringLiteral("compose.yml"), QStringLiteral("docker-compose.yaml"), QStringLiteral("docker-compose.yml")},
        QStringLiteral("Container Compose file"), ConfidenceLevel::Medium));
    registerDetector(marker(QStringLiteral("makefile"), 90,
        {QStringLiteral("makefile"), QStringLiteral("gnumakefile")}, QStringLiteral("Makefile"), ConfidenceLevel::Medium));
    registerDetector(marker(QStringLiteral("python"), 100,
        {QStringLiteral("pyproject.toml"), QStringLiteral("requirements.txt"), QStringLiteral("setup.py"),
         QStringLiteral("pipfile"), QStringLiteral("environment.yml"), QStringLiteral("environment.yaml")},
        QStringLiteral("Python project marker"), ConfidenceLevel::High));
    registerDetector(marker(QStringLiteral("readme"), 110, {QStringLiteral("readme")}, QStringLiteral("Project README"), ConfidenceLevel::Low, true));
    registerDetector(marker(QStringLiteral("sqlite"), 120,
        {QStringLiteral(".sqlite"), QStringLiteral(".sqlite3"), QStringLiteral(".db")},
        QStringLiteral("SQLite database candidate"), ConfidenceLevel::Medium, false, true, false, true));
    registerDetector(marker(QStringLiteral("log-file"), 130,
        {QStringLiteral(".log")}, QStringLiteral("Log file candidate; file contents were not opened"), ConfidenceLevel::Low,
        false, true));
}

WorkspaceDetectorRegistry::WorkspaceDetectorRegistry(QVector<std::shared_ptr<const IWorkspaceDetector>> detectors)
{
    for (auto& detector : detectors) registerDetector(std::move(detector));
}

void WorkspaceDetectorRegistry::registerDetector(std::shared_ptr<const IWorkspaceDetector> detector)
{
    if (!detector) return;
    const auto existing = std::find_if(m_detectors.cbegin(), m_detectors.cend(), [&](const auto& item) {
        return item->id() == detector->id();
    });
    if (existing != m_detectors.cend()) return;
    m_detectors.append(std::move(detector));
    std::stable_sort(m_detectors.begin(), m_detectors.end(), [](const auto& left, const auto& right) {
        if (left->priority() != right->priority()) return left->priority() < right->priority();
        return left->id().value() < right->id().value();
    });
}

QVector<DetectionEvidence> WorkspaceDetectorRegistry::inspect(const DetectionEntry& entry,
                                                               const ScanBudget& budget) const
{
    QVector<DetectionEvidence> output;
    for (const auto& detector : m_detectors) {
        auto found = detector->inspect(entry, budget);
        for (auto& evidence : found) output.append(std::move(evidence));
    }
    return output;
}

QString WorkspaceDetectorRegistry::outputVersion() const
{
    QStringList parts;
    for (const auto& detector : m_detectors)
        parts.append(QStringLiteral("%1:%2").arg(detector->id().value()).arg(detector->version()));
    return parts.join(QLatin1Char(';'));
}

} // namespace devhub::application::detection
