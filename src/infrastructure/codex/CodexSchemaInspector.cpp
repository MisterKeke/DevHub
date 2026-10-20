#include "CodexSchemaInspector.h"
#include "CodexSchemaSurface.h"
#include "CodexRuntimeProfile.h"
#include <QCryptographicHash>
#include <QDir>
#include <QHash>
#include <QMutex>
#include <QMutexLocker>
#include <QDateTime>
#include <QDirIterator>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStandardPaths>
#include <QThread>
#include <algorithm>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace devhub::infrastructure::codex {
using namespace application::assistant;
namespace {
constexpr qint64 bundleLimit = 32 * 1024 * 1024;
constexpr qint64 fileLimit = 4 * 1024 * 1024;
// Registry is deliberately memory-only, bounded and private to this TU. Native
// launch checks the binary again, including replacement at the same path.
QHash<QString, CodexCompatibilityReport>& acquired() { static QHash<QString, CodexCompatibilityReport> reports; return reports; }
QMutex& acquiredMutex() { static QMutex mutex; return mutex; }
QString evidenceKey(const QString& path, const QString& binary) { return path + QChar('|') + binary; }
QHash<QString, QString>& measuredBinaries() { static QHash<QString, QString> identities; return identities; }
QMutex& binaryMutex() { static QMutex mutex; return mutex; }
QString binaryStamp(const QString& path)
{
#ifdef Q_OS_WIN
    const auto native = QDir::toNativeSeparators(path);
    const auto handle = CreateFileW(reinterpret_cast<LPCWSTR>(native.utf16()), FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (handle == INVALID_HANDLE_VALUE) return {};
    BY_HANDLE_FILE_INFORMATION info{}; FILE_BASIC_INFO basic{};
    const bool valid = GetFileInformationByHandle(handle, &info)
        && GetFileInformationByHandleEx(handle, FileBasicInfo, &basic, sizeof(basic));
    CloseHandle(handle);
    if (!valid || (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) return {};
    // ChangeTime cannot be restored with ordinary timestamp preservation. File
    // identity also detects replacement with identical size and last-write time.
    return path + '|' + QString::number(info.dwVolumeSerialNumber) + '|' + QString::number(info.nFileIndexHigh)
        + '|' + QString::number(info.nFileIndexLow) + '|' + QString::number(info.nFileSizeHigh)
        + '|' + QString::number(info.nFileSizeLow) + '|' + QString::number(basic.CreationTime.QuadPart)
        + '|' + QString::number(basic.LastWriteTime.QuadPart) + '|' + QString::number(basic.ChangeTime.QuadPart);
#else
    Q_UNUSED(path);
    return {}; // No metadata-only trust fallback on unsupported platforms.
#endif
}
}
CodexSchemaInspector::CodexSchemaInspector(QObject* parent, int deadlineMilliseconds)
    : QObject(parent), m_deadlineMilliseconds(std::clamp(deadlineMilliseconds, 1, 120000))
{
    m_process.setParent(this); m_deadline.setParent(this); m_bounds.setParent(this);
    m_deadline.setSingleShot(true); m_bounds.setInterval(250);
    m_process.setProcessChannelMode(QProcess::SeparateChannels);
#ifdef Q_OS_WIN
    m_process.setCreateProcessArgumentsModifier([](QProcess::CreateProcessArguments* args) { args->flags |= CREATE_NO_WINDOW; });
#endif
    connect(&m_deadline, &QTimer::timeout, this, [this] {
        fail(CodexCompatibilityReason::SchemaDeadline, "Schema inspection exceeded its deadline.");
    });
    connect(&m_bounds, &QTimer::timeout, this, &CodexSchemaInspector::checkBounds);
    connect(&m_process, &QProcess::readyReadStandardOutput, this, &CodexSchemaInspector::drain);
    connect(&m_process, &QProcess::readyReadStandardError, this, &CodexSchemaInspector::drain);
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        if (m_active) fail(CodexCompatibilityReason::SchemaCommandUnsupported, "The executable could not generate its schema bundle.");
    });
    connect(&m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
        [this](int exitCode, QProcess::ExitStatus status) {
            if (!m_active) return;
            m_processTree.close();
            if (m_failure) { finish(*m_failure); return; }
            if (m_elapsed.elapsed() >= m_deadlineMilliseconds) {
                fail(CodexCompatibilityReason::SchemaDeadline, "Schema inspection exceeded its deadline."); return;
            }
            drain(); checkBounds(); if (!m_active) return;
            if (exitCode != 0 || status != QProcess::NormalExit) {
                fail(CodexCompatibilityReason::SchemaCommandUnsupported, "The executable does not support the required schema-generation command."); return;
            }
            const auto files = readBundle(); if (!m_active) return;
            auto report = CodexSchemaSurface::inspect(files);
            if (binaryIdentity(m_report.canonicalExecutable) != m_report.binaryIdentity) {
                fail(CodexCompatibilityReason::ExecutableChanged, "The executable changed during inspection. Recheck it before reconnecting."); return;
            }
            cache(files, report);
            if (m_elapsed.elapsed() >= m_deadlineMilliseconds) {
                fail(CodexCompatibilityReason::SchemaDeadline, "Schema inspection exceeded its deadline."); return;
            }
            finish(report);
        });
}
CodexSchemaInspector::~CodexSchemaInspector()
{
    disconnect(&m_process, nullptr, this, nullptr);
    m_processTree.close();
    if (m_process.state() != QProcess::NotRunning) { m_process.kill(); m_process.waitForFinished(100); }
    // Do not recursively remove a directory while its generating process lives.
    if (m_directory && m_process.state() != QProcess::NotRunning) m_directory->setAutoRemove(false);
}
QString CodexSchemaInspector::binaryIdentity(const QString& path)
{
    const auto stamp = binaryStamp(path);
    if (!stamp.isEmpty()) {
        QMutexLocker lock(&binaryMutex());
        const auto found = measuredBinaries().constFind(stamp);
        if (found != measuredBinaries().cend()) return *found;
    }
    const QFileInfo before(path);
    if (!before.isFile() || before.size() <= 0 || before.size() > 512 * 1024 * 1024) return {};
    QFile file(path); if (!file.open(QIODevice::ReadOnly)) return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    QElapsedTimer deadline; deadline.start(); qint64 total = 0;
    while (!file.atEnd()) {
        const auto bytes = file.read(1024 * 1024);
        if (bytes.isEmpty() || deadline.elapsed() > 5000 || QThread::currentThread()->isInterruptionRequested()) return {};
        total += bytes.size(); hash.addData(bytes);
    }
    const QFileInfo after(path);
    if (file.error() != QFile::NoError || total != before.size() || before.size() != after.size()
        || before.lastModified() != after.lastModified() || before.canonicalFilePath() != after.canonicalFilePath()) return {};
    if (!stamp.isEmpty() && stamp != binaryStamp(path)) return {};
    const auto identity = QString::fromLatin1(hash.result().toHex());
    if (!stamp.isEmpty()) {
        QMutexLocker lock(&binaryMutex());
        if (measuredBinaries().size() >= 32) measuredBinaries().clear();
        measuredBinaries().insert(stamp, identity);
    }
    return identity;
}
bool CodexSchemaInspector::matchesBinaryIdentity(const QString& path, const QString& identity)
{
    const auto stamp = binaryStamp(path);
    if (stamp.isEmpty() || identity.isEmpty()) return false;
    QMutexLocker lock(&binaryMutex());
    return measuredBinaries().value(stamp) == identity;
}
std::optional<CodexCompatibilityReport> CodexSchemaInspector::evidence(const CodexExecutable& executable)
{
    const auto path = QFileInfo(executable.path).canonicalFilePath();
    const auto binary = executable.compatibility.binaryIdentity;
    if (!matchesBinaryIdentity(path, binary)) return std::nullopt;
    QMutexLocker lock(&acquiredMutex());
    const auto found = acquired().constFind(evidenceKey(path, binary));
    if (found == acquired().cend() || found->reportedVersion != executable.version
        || found->schemaIdentity != executable.compatibility.schemaIdentity || found->adapter != executable.compatibility.adapter) return std::nullopt;
    return *found;
}
void CodexSchemaInspector::inspect(const QString& path, const QString& version)
{
    if (m_active || m_process.state() != QProcess::NotRunning) return;
    m_report = {}; m_failure.reset(); m_report.canonicalExecutable = QFileInfo(path).canonicalFilePath();
    m_report.reportedVersion = version; m_report.binaryIdentity = binaryIdentity(path); m_active = true;
    // Invalidate previous evidence immediately, even if this acquisition fails.
    { QMutexLocker lock(&acquiredMutex()); acquired().remove(evidenceKey(m_report.canonicalExecutable, m_report.binaryIdentity)); }
    if (m_report.binaryIdentity.isEmpty()) {
        fail(CodexCompatibilityReason::ExecutableMissing, "The executable could not be identified within the inspection limits."); return;
    }
    m_elapsed.start(); m_deadline.start(m_deadlineMilliseconds); m_bounds.start(); startMode();
}
void CodexSchemaInspector::startMode()
{
    m_outputBytes = 0;
    m_directory = std::make_unique<QTemporaryDir>(QDir::tempPath() + "/devhub-codex-schema-XXXXXX");
    if (!m_directory->isValid()) { fail(CodexCompatibilityReason::SchemaMalformed, "A private schema directory could not be created."); return; }
    QDir root(m_directory->path()); root.mkdir("home"); root.mkdir("bundle");
    auto environment = codexBaseEnvironment(QProcessEnvironment::systemEnvironment());
    environment.insert("CODEX_HOME", root.filePath("home"));
    // Every implemented adapter needs experimental restriction fields. Generate
    // that surface directly instead of spending the deadline on a stable bundle
    // that cannot match any adapter, then generating the entire bundle again.
    QStringList arguments{"app-server", "generate-json-schema", "--out", root.filePath("bundle"), "--experimental"};
    if (!m_processTree.prepare(m_process)) {
        fail(CodexCompatibilityReason::SchemaCommandUnsupported, "Bounded schema inspection is unavailable on this system."); return;
    }
    m_process.setProgram(m_report.canonicalExecutable); m_process.setArguments(arguments);
    m_process.setWorkingDirectory(root.path()); m_process.setProcessEnvironment(environment); m_process.start();
}
void CodexSchemaInspector::drain()
{
    if (!m_active || m_failure) return;
    for (const auto channel : {QProcess::StandardOutput, QProcess::StandardError}) {
        m_process.setReadChannel(channel);
        m_outputBytes += m_process.read(65537).size(); // discard, never log executable output
    }
    if (m_outputBytes > 64 * 1024) fail(CodexCompatibilityReason::SchemaLimit, "Schema command output exceeded its limit.");
}
void CodexSchemaInspector::checkBounds()
{
    if (!m_active || m_failure || !m_directory) return;
    QDirIterator files(m_directory->path(), QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System, QDirIterator::Subdirectories);
    int count = 0; qint64 bytes = 0;
    while (files.hasNext()) {
        files.next(); const auto info = files.fileInfo();
        bytes += info.size();
        if (++count > 2048 || bytes > bundleLimit || info.isSymLink() || (!info.isDir() && !info.isFile()) || info.size() > fileLimit) {
            fail(CodexCompatibilityReason::SchemaLimit, "Generated schema files exceeded their limits or contained unsafe links."); return;
        }
    }
}
QJsonObject CodexSchemaInspector::readBundle()
{
    QJsonObject result;
    const QDir root(m_directory->filePath("bundle"));
    QDirIterator paths(root.path(), {"*.json"}, QDir::Files, QDirIterator::Subdirectories);
    qint64 bytes = 0; int count = 0;
    while (paths.hasNext()) {
        const auto path = paths.next(); QFile file(path);
        if (++count > 1024 || !file.open(QIODevice::ReadOnly)) break;
        const auto data = file.read(fileLimit + 1); bytes += data.size();
        QJsonParseError error; const auto document = QJsonDocument::fromJson(data, &error);
        if (data.size() > fileLimit || bytes > bundleLimit || file.error() != QFile::NoError
            || error.error != QJsonParseError::NoError || !document.isObject()) {
            fail(CodexCompatibilityReason::SchemaMalformed, "The generated schema is malformed, truncated, or too large."); return {};
        }
        result.insert(root.relativeFilePath(path), document.object());
    }
    if (count > 1024 || result.isEmpty() || count != result.size()) {
        fail(CodexCompatibilityReason::SchemaMalformed, "The generated schema bundle is incomplete."); return {};
    }
    return result;
}
void CodexSchemaInspector::cache(const QJsonObject& files, CodexCompatibilityReport& report)
{
    // A cache assertion is never authority: fresh acquisition + comparison above
    // is mandatory. Store raw schemas only, atomically, for bounded reuse/diagnosis.
    const auto path = QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/codex-compatibility-v1";
    if (!QDir().mkpath(path) || QFileInfo(path).isSymLink()) return;
    const auto key = CodexSchemaSurface::identity(QJsonObject{{"binary", m_report.binaryIdentity},
        {"adapterRevision", "surface-v1"}, {"schema", CodexSchemaSurface::identity(files)}});
    const auto filename = QDir(path).filePath(key + ".json");
    if (QFileInfo(filename).isSymLink()) return;
    QFile previous(filename);
    if (previous.open(QIODevice::ReadOnly) && previous.size() <= bundleLimit) {
        QJsonParseError error; const auto saved = QJsonDocument::fromJson(previous.read(bundleLimit + 1), &error);
        if (error.error == QJsonParseError::NoError && saved.object() == files) report.evidence << "fresh-schema-cache-match";
        else report.evidence << "corrupt-cache-discarded";
    }
    previous.close();
    QSaveFile output(filename);
    if (output.open(QIODevice::WriteOnly)) { const auto data = QJsonDocument(files).toJson(QJsonDocument::Compact);
        if (output.write(data) == data.size()) (void)output.commit(); else output.cancelWriting(); }
    // Only this dedicated directory's regular digest-named files are evicted.
    const auto entries = QDir(path).entryInfoList({"*.json"}, QDir::Files | QDir::NoSymLinks, QDir::Time);
    for (qsizetype i = 16; i < entries.size(); ++i) {
        const auto name = entries.at(i).completeBaseName();
        if (name.size() == 64 && QByteArray::fromHex(name.toLatin1()).toHex() == name.toLatin1()) QFile::remove(entries.at(i).absoluteFilePath());
    }
}
void CodexSchemaInspector::finish(CodexCompatibilityReport report)
{
    if (!m_active) return;
    report.canonicalExecutable = m_report.canonicalExecutable; report.reportedVersion = m_report.reportedVersion;
    report.binaryIdentity = m_report.binaryIdentity;
    m_active = false; m_deadline.stop(); m_bounds.stop();
    if (m_process.state() == QProcess::NotRunning) m_processTree.close();
    if (report.protocolCompatible) {
        QMutexLocker lock(&acquiredMutex());
        if (acquired().size() >= 16) acquired().clear();
        acquired().insert(evidenceKey(report.canonicalExecutable, report.binaryIdentity), report);
    }
    emit completed(report);
}
void CodexSchemaInspector::fail(CodexCompatibilityReason reason, const QString& explanation)
{
    if (!m_active || m_failure) return;
    auto report = m_report; report.reason = reason; report.explanation = explanation;
    report.state = reason == CodexCompatibilityReason::Cancelled || reason == CodexCompatibilityReason::SchemaDeadline
        ? CodexCompatibilityState::VerificationRequired : CodexCompatibilityState::Incompatible;
    m_failure = report; m_deadline.stop(); m_bounds.stop();
    if (m_process.state() == QProcess::NotRunning) finish(report);
    else { m_processTree.close(); m_process.kill(); } // finish only after the child has stopped
}
void CodexSchemaInspector::cancel()
{
    fail(CodexCompatibilityReason::Cancelled, "Schema inspection was cancelled.");
}
}
