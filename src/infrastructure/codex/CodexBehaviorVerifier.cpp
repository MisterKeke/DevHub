#include "CodexBehaviorVerifier.h"
#include "CodexSchemaInspector.h"
#include "CodexSchemaSurface.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QStandardPaths>
#include <QSaveFile>
#include <bit>
#include <utility>
#ifdef Q_OS_WIN
#include <windows.h>
#include <wincrypt.h>
#endif

static void initializeCodexVerificationResource() {
    static const bool initialized = [] { Q_INIT_RESOURCE(codex_verification); return true; }();
    Q_UNUSED(initialized);
}
namespace devhub::infrastructure::codex {
namespace {
constexpr auto evidenceFormat = "devhub-protected-behavior-v1";
QString evidencePath(const QString& key)
{
    if (key.isEmpty()) return {};
    const auto root = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    return root.isEmpty() ? QString{} : QDir(root).filePath("codex-compatibility/behavior-v1/" + key + ".bin");
}
QByteArray protectEvidence(const QByteArray& bytes, const QString& key, bool encrypt)
{
#ifdef Q_OS_WIN
    // Resolve only the Windows system DLL; no linker/runtime dependency on an
    // application-local cryptography library or user-supplied key material.
    static const HMODULE module = LoadLibraryExW(L"crypt32.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    using Protect = decltype(&CryptProtectData);
    using Unprotect = decltype(&CryptUnprotectData);
    static const auto protect = module ? std::bit_cast<Protect>(GetProcAddress(module, "CryptProtectData")) : nullptr;
    static const auto unprotect = module ? std::bit_cast<Unprotect>(GetProcAddress(module, "CryptUnprotectData")) : nullptr;
    if (!protect || !unprotect || bytes.isEmpty() || bytes.size() > 64 * 1024) return {};
    const auto entropy = QCryptographicHash::hash(QByteArray(evidenceFormat) + key.toUtf8(), QCryptographicHash::Sha256);
    DATA_BLOB input{static_cast<DWORD>(bytes.size()), reinterpret_cast<BYTE*>(const_cast<char*>(bytes.constData()))};
    DATA_BLOB salt{static_cast<DWORD>(entropy.size()), reinterpret_cast<BYTE*>(const_cast<char*>(entropy.constData()))};
    DATA_BLOB output{};
    const bool ok = encrypt ? protect(&input, nullptr, &salt, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output)
        : unprotect(&input, nullptr, &salt, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &output);
    QByteArray result;
    if (ok && output.cbData <= 64 * 1024) result = QByteArray(reinterpret_cast<const char*>(output.pbData), static_cast<qsizetype>(output.cbData));
    if (output.pbData) { SecureZeroMemory(output.pbData, output.cbData); LocalFree(output.pbData); }
    return result;
#else
    Q_UNUSED(bytes); Q_UNUSED(key); Q_UNUSED(encrypt);
    return {}; // No plaintext trust fallback on unsupported platforms.
#endif
}
}
QString CodexBehaviorVerifier::evidenceIdentity(const application::assistant::CodexCompatibilityReport& report)
{
    initializeCodexVerificationResource();
    if (!report.protocolCompatible || report.canonicalExecutable.isEmpty() || report.reportedVersion.isEmpty()
        || report.binaryIdentity.isEmpty() || report.adapter.isEmpty() || report.schemaIdentity.isEmpty()
        || report.configurationIdentity.isEmpty() || report.policyIdentity.isEmpty()) return {};
    // Hash the actual embedded harness, so changes invalidate prior evidence
    // without requiring verification for an unrelated UI/debug rebuild.
    static const auto harness = [] {
        QCryptographicHash hash(QCryptographicHash::Sha256);
        for (const auto* name : {"probe-codex-profile.py", "verify-codex-capabilities.py", "verify-codex-read-tools.py", "codex-inventory-identity.py"}) {
            QFile file(QStringLiteral(":/codex-verification/") + name);
            if (!file.open(QIODevice::ReadOnly)) return QString{};
            hash.addData(QByteArray(name)); hash.addData(file.readAll());
        }
        return QString::fromLatin1(hash.result().toHex());
    }();
    if (harness.isEmpty()) return {};
    return CodexSchemaSurface::identity(QJsonObject{{"format", evidenceFormat}, {"harness", harness},
        {"path", report.canonicalExecutable}, {"version", report.reportedVersion}, {"binary", report.binaryIdentity},
        {"adapter", report.adapter}, {"schema", report.schemaIdentity}, {"configuration", report.configurationIdentity},
        {"policyAndTools", report.policyIdentity}});
}
bool CodexBehaviorVerifier::cachedEvidenceMatches(const application::assistant::CodexCompatibilityReport& report)
{
    const auto key = evidenceIdentity(report);
    if (key.isEmpty() || !CodexSchemaInspector::matchesBinaryIdentity(report.canonicalExecutable, report.binaryIdentity)) return false;
    const auto path = evidencePath(key); const QFileInfo info(path);
    if (!info.isFile() || info.isSymLink() || info.size() <= 0 || info.size() > 64 * 1024) return false;
    QFile file(path); if (!file.open(QIODevice::ReadOnly)) return false;
    const auto plain = protectEvidence(file.read(64 * 1024 + 1), key, false);
    // Exact payload comparison provides an additional application integrity
    // check after DPAPI authentication; no editable boolean is consulted.
    return !plain.isEmpty() && plain == QJsonDocument(QJsonObject{{"format", evidenceFormat}, {"evidence", key}, {"verified", true}}).toJson(QJsonDocument::Compact);
}
bool CodexBehaviorVerifier::rememberVerifiedEvidence(const application::assistant::CodexCompatibilityReport& report)
{
    const auto key = evidenceIdentity(report);
    if (key.isEmpty() || !report.runtimePolicyVerified
        || !CodexSchemaInspector::matchesBinaryIdentity(report.canonicalExecutable, report.binaryIdentity)) return false;
    const auto bytes = protectEvidence(QJsonDocument(QJsonObject{{"format", evidenceFormat}, {"evidence", key}, {"verified", true}})
        .toJson(QJsonDocument::Compact), key, true);
    if (bytes.isEmpty()) return false;
    const auto path = evidencePath(key);
    if (path.isEmpty() || !QDir().mkpath(QFileInfo(path).absolutePath())) return false;
    const QFileInfo existing(path);
    if (existing.isSymLink() || (existing.exists() && !existing.isFile())) return false;
    QSaveFile file(path); file.setDirectWriteFallback(false);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size() && file.commit();
}
void CodexBehaviorVerifier::forgetCachedEvidence(const application::assistant::CodexCompatibilityReport& report)
{
    const auto path = evidencePath(evidenceIdentity(report));
    if (!path.isEmpty()) QFile::remove(path);
}
CodexBehaviorVerifier::CodexBehaviorVerifier(QObject* parent) : QObject(parent)
{
    initializeCodexVerificationResource(); m_deadline.setSingleShot(true);
    connect(&m_deadline, &QTimer::timeout, this, [this] { m_limitExceeded = true; cancel(); });
    connect(&m_process, &QProcess::readyReadStandardOutput, this, &CodexBehaviorVerifier::drain);
    connect(&m_process, &QProcess::readyReadStandardError, this, &CodexBehaviorVerifier::drain);
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart && m_active) finish(false, "The isolated verification interpreter could not start.");
    });
    connect(&m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished), this,
        [this](int code, QProcess::ExitStatus status) {
            if (!m_active) return;
            drain(); closeProcessTree();
            if (m_elapsed.elapsed() >= 180000) m_limitExceeded = true;
            if (m_cancelled || m_limitExceeded) { finish(false, m_limitExceeded ? "Behavior verification exceeded its limits." : "Behavior verification was cancelled."); return; }
            QFile output(m_directory->filePath(m_stage == 0 ? "capabilities.json" : "tools.json"));
            QJsonParseError error;
            if (!output.open(QIODevice::ReadOnly) || output.size() > 1024 * 1024) {
                finish(false, code != 0 || status != QProcess::NormalExit
                    ? failedCheckDescription({}, m_stage == 1) : "Behavior evidence was missing or exceeded its limit."); return;
            }
            const auto result = QJsonDocument::fromJson(output.read(1024 * 1024 + 1), &error).object();
            const auto checks = result.value("checks").toObject();
            if (code != 0 || status != QProcess::NormalExit) { finish(false, failedCheckDescription(checks, m_stage == 1)); return; }
            bool accepted = error.error == QJsonParseError::NoError && result.value("fixtureScopePassed") == true
                && result.value("realInference") == false && result.value("releaseEligible") == false
                && result.value("codexVersion") == m_report.reportedVersion && !checks.isEmpty();
            if (m_stage == 0) accepted = accepted && result.value("runtimePolicy") == "restricted-tools-v2";
            for (auto it = checks.begin(); it != checks.end(); ++it) accepted = accepted && it.value() == true;
            if (!accepted) { finish(false, failedCheckDescription(checks, m_stage == 1)); return; }
            if (m_stage++ == 0 && m_verifyTools) { runStage(); return; }
            if (!CodexSchemaInspector::matchesBinaryIdentity(m_report.canonicalExecutable, m_report.binaryIdentity)) {
                finish(false, "The executable changed during behavior verification."); return;
            }
            if (m_elapsed.elapsed() >= 180000) { finish(false, "Behavior verification exceeded its deadline."); return; }
            finish(true, "Offline behavior checks verified disabled native tools and the DevHub tool inventory.");
        });
}
CodexBehaviorVerifier::~CodexBehaviorVerifier()
{
    disconnect(&m_process, nullptr, this, nullptr); closeProcessTree();
    if (m_process.state() != QProcess::NotRunning) { m_process.kill(); m_process.waitForFinished(100); }
    if (m_directory && m_process.state() != QProcess::NotRunning) m_directory->setAutoRemove(false);
}
bool CodexBehaviorVerifier::start(const application::assistant::CodexCompatibilityReport& report, const QJsonArray& tools)
{
    if (m_active || !report.protocolCompatible || report.binaryIdentity.isEmpty()
        || !CodexSchemaInspector::matchesBinaryIdentity(report.canonicalExecutable, report.binaryIdentity)) return false;
    // This optional verifier needs a native Python 3 interpreter. No downloads,
    // shell wrappers, user's Python modules or editable helper scripts are used.
    // Windows' python3 alias can point at the Microsoft Store launcher even
    // when a working python.exe is installed. Prefer the native interpreter.
#ifdef Q_OS_WIN
    m_python = QStandardPaths::findExecutable("python");
    if (m_python.isEmpty() || QFileInfo(m_python).size() == 0) m_python = QStandardPaths::findExecutable("python3");
#else
    m_python = QStandardPaths::findExecutable("python3");
    if (m_python.isEmpty()) m_python = QStandardPaths::findExecutable("python");
#endif
    if (m_python.isEmpty()) return false;
#ifdef Q_OS_WIN
    if (QFileInfo(m_python).size() <= 0 || QFileInfo(m_python).suffix().compare("exe", Qt::CaseInsensitive) != 0) return false;
#else
    // Tree containment is currently implemented with Windows job objects.
    return false;
#endif
    m_directory = std::make_unique<QTemporaryDir>(QDir::tempPath() + "/devhub-codex-behavior-XXXXXX");
    if (!m_directory->isValid()) return false;
    for (const auto& name : {"probe-codex-profile.py", "verify-codex-capabilities.py", "verify-codex-read-tools.py", "codex-inventory-identity.py"}) {
        QFile source(QStringLiteral(":/codex-verification/") + name), target(m_directory->filePath(name));
        if (!source.open(QIODevice::ReadOnly) || !target.open(QIODevice::WriteOnly)) return false;
        const auto data = source.readAll(); if (target.write(data) != data.size()) return false;
    }
    QFile inventory(m_directory->filePath("inventory.json"));
    if (!inventory.open(QIODevice::WriteOnly)) return false;
    const auto bytes = QJsonDocument(tools).toJson(QJsonDocument::Compact);
    if (inventory.write(bytes) != bytes.size()) return false;
    inventory.close();
    m_report = report; m_verifyTools = !tools.isEmpty(); m_stage = 0; m_outputBytes = 0; m_cancelled = false; m_limitExceeded = false;
    m_active = true; m_elapsed.start(); m_deadline.start(180000); runStage(); return true;
}
void CodexBehaviorVerifier::runStage()
{
    if (!m_processTree.prepare(m_process)) {
        finish(false, "The isolated verification process tree could not be contained."); return;
    }
    const auto name = m_stage == 0 ? "verify-codex-capabilities.py" : "verify-codex-read-tools.py";
    QStringList arguments{"-I", "-B", m_directory->filePath(name), "--executable", m_report.canonicalExecutable,
        "--version", m_report.reportedVersion, "--output", m_directory->filePath(m_stage == 0 ? "capabilities.json" : "tools.json")};
    if (m_stage == 1) arguments << "--specifications" << m_directory->filePath("inventory.json");
    m_process.setProgram(m_python); m_process.setArguments(arguments);
    m_process.setProcessEnvironment(application::assistant::codexBaseEnvironment(QProcessEnvironment::systemEnvironment()));
    m_process.setWorkingDirectory(m_directory->path()); m_process.start();
    // The helper is noninteractive. Descendant CLI commands must also receive
    // EOF instead of waiting on a pipe owned by the desktop event loop.
    m_process.closeWriteChannel();
}
QString CodexBehaviorVerifier::failedCheckDescription(const QJsonObject& checks, bool toolInventory)
{
    // Surface only reviewed labels. Raw helper output can contain executable
    // paths and synthetic protocol payloads and never belongs in the UI.
    const std::pair<const char*, const char*> labels[] = {
        {"nativePositiveControlReadBothMarkers", "Native execution control failed."},
        {"imagePositiveControlReadImage", "Image access control failed."},
        {"disabledCallsRejectedWithoutReads", "Disabled native tool restrictions could not be verified."},
        {"mcpPositiveAndNegativeControl", "Inherited MCP isolation could not be verified."},
        {"emptyMcpTableDoesNotDisableInheritance", "Inherited MCP control failed."},
        {"pluginPositiveAndNegativeControl", "Plugin isolation could not be verified."},
        {"hookCliPositiveAndNegativeControl", "Hook verification did not complete successfully."},
        {"emptyEnvironmentPolicyPersists", "Conversation environment restrictions could not be verified."},
        {"hardenedModelSurfaceHasNoIoTools", "Restricted model tools could not be verified."},
        {"taggedSpecAccepted", "Codex did not accept the DevHub tool definitions."},
        {"turnCompleted", "The isolated DevHub tool turn did not complete."},
        {"singleValidCall", "The isolated DevHub tool call was not valid."},
        {"restrictedOfferedSurface", "The DevHub tool inventory did not match."},
        {"responseConsumed", "Codex did not consume the isolated DevHub tool response."},
        {"protocolCompleted", "The isolated DevHub tool protocol did not complete."},
        {"allCasesCompleted", "One or more isolated behavior checks did not complete."},
    };
    for (const auto& [key, label] : labels)
        if (checks.contains(QLatin1String(key)) && checks.value(QLatin1String(key)) != true)
            return QString::fromLatin1(label) + QStringLiteral(" Sending remains disabled until verification succeeds.");
    return toolInventory ? QStringLiteral("The isolated DevHub tool checks failed. Sending remains disabled until verification succeeds.")
        : QStringLiteral("The isolated capability checks failed. Sending remains disabled until verification succeeds.");
}
void CodexBehaviorVerifier::drain()
{
    for (const auto channel : {QProcess::StandardOutput, QProcess::StandardError}) {
        m_process.setReadChannel(channel); m_outputBytes += m_process.read(65537).size();
    }
    if (m_outputBytes > 1024 * 1024) { m_limitExceeded = true; cancel(); }
}
void CodexBehaviorVerifier::closeProcessTree()
{
    m_processTree.close();
}
void CodexBehaviorVerifier::cancel()
{
    if (!m_active) return;
    m_cancelled = true; closeProcessTree();
    if (m_process.state() == QProcess::NotRunning) finish(false, "Behavior verification was cancelled.");
    else m_process.kill();
}
void CodexBehaviorVerifier::finish(bool accepted, const QString& reason)
{
    if (!m_active) return;
    m_active = false; m_deadline.stop(); closeProcessTree(); emit completed(accepted, reason);
}
}
