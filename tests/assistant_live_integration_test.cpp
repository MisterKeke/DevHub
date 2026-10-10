#include "application/assistant/AssistantController.h"
#include "application/assistant/IConversationRepository.h"
#include "application/assistant/AssistantToolRuntime.h"
#include "application/git/GitExecutableLocator.h"
#include "application/git/GitService.h"
#include "application/services/ManagedProcess.h"
#include "application/services/ServiceCoordinator.h"
#include "application/services/IServiceDefinitionRepository.h"
#include "application/tasks/TaskScheduler.h"
#include "application/workspaces/IWorkspaceRepository.h"
#include "application/workspaces/WorkspaceContext.h"
#include "application/workspaces/WorkspaceManager.h"
#include "application/workspaces/WorkspaceValidation.h"
#include "application/notes/INotesRepository.h"
#include "infrastructure/codex/CodexClient.h"
#include "infrastructure/codex/CodexExecutableLocator.h"
#include "infrastructure/codex/CodexProcess.h"
#include "infrastructure/codex/CodexRuntimeProfile.h"
#include "infrastructure/codex/CodexVersionSupport.h"
#include "shared/identifiers/IdGenerator.h"
#include "shared/time/ApplicationClock.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QSignalSpy>
#include <QSet>
#include <QTemporaryDir>
#include <QTest>

#include <memory>

using namespace devhub::application::assistant;
using namespace devhub::infrastructure::codex;

namespace {
constexpr auto kOptInName = "DEVHUB_RUN_LIVE_ASSISTANT_TESTS";
constexpr auto kOptInValue = "I_ACKNOWLEDGE_REAL_CODEX_INFERENCE";
constexpr auto kExecutableName = "DEVHUB_LIVE_CODEX_EXE";
constexpr auto kHomeName = "DEVHUB_LIVE_CODEX_HOME";
constexpr auto kDisposableAckName = "DEVHUB_LIVE_DISPOSABLE_PROFILE_ACK";
constexpr auto kDisposableAckValue = "I_CREATED_THIS_DISPOSABLE_PROFILE";
constexpr auto kProfileMarker = ".devhub-disposable-codex-profile";
constexpr auto kProfileMarkerValue = "DevHub live integration disposable profile\n";
constexpr auto kTestAccountScope = "92f343d4-5b1e-41ac-9c83-99f6410e1b3f";
constexpr auto kCompletionPrompt =
    "For this disposable integration check, reply with exactly the single word READY.";
constexpr auto kStopPrompt =
    "For this disposable integration check, begin a long harmless explanation of how rain forms. "
    "Continue in detail until interrupted; do not call tools or access files.";

class MemoryConversationRepository final : public IConversationRepository {
public:
    devhub::shared::common::Result<QList<AssistantConversationMetadata>> list(
        const QString& accountId, int limit) override
    {
        QList<AssistantConversationMetadata> result;
        for (auto it = m_rows.crbegin(); it != m_rows.crend() && result.size() < limit; ++it) {
            if (it->devhubAccountId == accountId) result.append(*it);
        }
        return devhub::shared::common::Result<QList<AssistantConversationMetadata>>::success(result);
    }

    devhub::shared::common::Result<void> save(const AssistantConversationMetadata& metadata) override
    {
        for (auto& row : m_rows) {
            if (row.conversation.id == metadata.conversation.id) {
                row = metadata;
                return devhub::shared::common::Result<void>::success();
            }
        }
        m_rows.append(metadata);
        return devhub::shared::common::Result<void>::success();
    }

    [[nodiscard]] bool hasAssociation() const { return !m_rows.isEmpty(); }

private:
    QList<AssistantConversationMetadata> m_rows;
};

devhub::domain::errors::AppError unusedToolFixtureError()
{
    return devhub::domain::errors::AppError::failure(devhub::domain::errors::ErrorCode::InvalidArgument,
        devhub::domain::errors::ErrorCategory::Internal, QStringLiteral("Unused live tool fixture operation."), {},
        QStringLiteral("live test fixture"));
}

class LiveWorkspaceRepository final : public devhub::application::workspaces::IWorkspaceRepository {
public:
    devhub::shared::common::Result<QVector<devhub::domain::workspace::Workspace>> listForAccount(
        const devhub::domain::account::AccountId&) override
    { return devhub::shared::common::Result<QVector<devhub::domain::workspace::Workspace>>::success(rows); }
    devhub::shared::common::Result<devhub::domain::workspace::Workspace> add(const devhub::domain::workspace::Workspace&) override
    { return devhub::shared::common::Result<devhub::domain::workspace::Workspace>::failure(unusedToolFixtureError()); }
    devhub::shared::common::Result<void> rename(const devhub::domain::workspace::WorkspaceId&, const devhub::domain::account::AccountId&, const QString&) override
    { return devhub::shared::common::Result<void>::failure(unusedToolFixtureError()); }
    devhub::shared::common::Result<void> setFavorite(const devhub::domain::workspace::WorkspaceId&, const devhub::domain::account::AccountId&, bool) override
    { return devhub::shared::common::Result<void>::failure(unusedToolFixtureError()); }
    devhub::shared::common::Result<void> updateLocation(const devhub::domain::workspace::WorkspaceId&, const devhub::domain::account::AccountId&, const devhub::domain::workspace::WorkspacePath&) override
    { return devhub::shared::common::Result<void>::failure(unusedToolFixtureError()); }
    devhub::shared::common::Result<void> remove(const devhub::domain::workspace::WorkspaceId&, const devhub::domain::account::AccountId&) override
    { return devhub::shared::common::Result<void>::failure(unusedToolFixtureError()); }
    devhub::shared::common::Result<void> recordOpened(const devhub::domain::workspace::WorkspaceId&, const devhub::domain::account::AccountId&, const QDateTime&) override
    { return devhub::shared::common::Result<void>::success(); }
    devhub::shared::common::Result<void> setRecentOrder(const devhub::domain::account::AccountId&, const QVector<devhub::domain::workspace::WorkspaceId>&) override
    { return devhub::shared::common::Result<void>::success(); }
    QVector<devhub::domain::workspace::Workspace> rows;
};

class LiveServiceRepository final : public devhub::application::services::IServiceDefinitionRepository {
public:
    devhub::shared::common::Result<QVector<devhub::domain::services::ServiceDefinition>> list(const QString&) override
    { return devhub::shared::common::Result<QVector<devhub::domain::services::ServiceDefinition>>::success({}); }
    devhub::shared::common::Result<void> save(const devhub::domain::services::ServiceDefinition&) override
    { return devhub::shared::common::Result<void>::failure(unusedToolFixtureError()); }
    devhub::shared::common::Result<void> remove(const QString&, const QString&) override
    { return devhub::shared::common::Result<void>::failure(unusedToolFixtureError()); }
};

class LiveProcessLauncher final : public devhub::application::services::IProcessLauncher {
public:
    devhub::application::services::IManagedProcess* launch(
        const devhub::domain::services::ServiceDefinition&, QObject*) override { return nullptr; }
};

class LiveNotesRepository final : public devhub::application::notes::INotesRepository {
public:
    devhub::shared::common::Result<QVector<devhub::application::notes::NoteNode>> list(const QString&, bool) override
    { return devhub::shared::common::Result<QVector<devhub::application::notes::NoteNode>>::success({}); }
    devhub::shared::common::Result<devhub::application::notes::NoteNode> create(const QString&, QString, QString, devhub::application::notes::NoteNodeKind) override
    { return devhub::shared::common::Result<devhub::application::notes::NoteNode>::failure(unusedToolFixtureError()); }
    devhub::shared::common::Result<void> rename(const QString&, const QString&, QString) override { return devhub::shared::common::Result<void>::failure(unusedToolFixtureError()); }
    devhub::shared::common::Result<void> move(const QString&, const QString&, QString) override { return devhub::shared::common::Result<void>::failure(unusedToolFixtureError()); }
    devhub::shared::common::Result<void> setPinned(const QString&, const QString&, bool) override { return devhub::shared::common::Result<void>::failure(unusedToolFixtureError()); }
    devhub::shared::common::Result<void> setTags(const QString&, const QString&, QStringList) override { return devhub::shared::common::Result<void>::failure(unusedToolFixtureError()); }
    devhub::shared::common::Result<QString> readContent(const QString&, const QString&) override { return devhub::shared::common::Result<QString>::failure(unusedToolFixtureError()); }
    devhub::shared::common::Result<QString> readContentBounded(const QString&, const QString&, qint64) override { return devhub::shared::common::Result<QString>::failure(unusedToolFixtureError()); }
    devhub::shared::common::Result<QString> saveContent(const QString&, const QString&, const QString&, const QString&) override { return devhub::shared::common::Result<QString>::failure(unusedToolFixtureError()); }
    QString contentPath(const QString&, const QString&) const override { return {}; }
    devhub::shared::common::Result<void> setTrashed(const QString&, const QString&, bool) override { return devhub::shared::common::Result<void>::failure(unusedToolFixtureError()); }
    devhub::shared::common::Result<void> permanentlyRemove(const QString&, const QString&) override { return devhub::shared::common::Result<void>::failure(unusedToolFixtureError()); }
};

QString canonicalPath(const QString& path)
{
    return QDir::cleanPath(QFileInfo(path).canonicalFilePath());
}

bool pathsOverlap(const QString& left, const QString& right)
{
    const auto normalizedLeft = QDir::fromNativeSeparators(canonicalPath(left)).toCaseFolded();
    const auto normalizedRight = QDir::fromNativeSeparators(canonicalPath(right)).toCaseFolded();
    return normalizedLeft == normalizedRight || normalizedLeft.startsWith(normalizedRight + '/')
        || normalizedRight.startsWith(normalizedLeft + '/');
}

QString identityKey(const AssistantMessage& message)
{
    const auto role = message.role == AssistantRole::User ? QStringLiteral("user") : QStringLiteral("assistant");
    const auto item = message.itemId.isEmpty() && message.role == AssistantRole::User
        ? QStringLiteral("user-item") : message.itemId;
    return message.threadId + QLatin1Char('\n') + message.turnId + QLatin1Char('\n') + role
        + QLatin1Char('\n') + item;
}
} // namespace

class AssistantLiveIntegrationTest final : public QObject {
    Q_OBJECT

private slots:
    void liveHandshakeStreamingStopAndResume()
    {
        m_started.start();
        QVERIFY2(CodexRuntimeProfile::developmentLiveValidationBuild(),
            "The live harness must link the dedicated validation-only Assistant library.");
        const auto environment = QProcessEnvironment::systemEnvironment();
        if (environment.value(QLatin1String(kOptInName)) != QLatin1String(kOptInValue)) {
            QSKIP("Live Codex inference is disabled. Set the documented explicit opt-in only for a disposable profile.");
        }

        m_executable = environment.value(QLatin1String(kExecutableName));
        m_codexHome = environment.value(QLatin1String(kHomeName));
        if (environment.value(QLatin1String(kDisposableAckName)) != QLatin1String(kDisposableAckValue)
            || m_executable.isEmpty() || m_codexHome.isEmpty()) {
            QSKIP("Explicit executable, isolated CODEX_HOME, and disposable-profile acknowledgement are required.");
        }

        const QFileInfo executableInfo(m_executable);
        const QFileInfo homeInfo(m_codexHome);
        if (!executableInfo.isAbsolute() || !executableInfo.isFile() || !executableInfo.isExecutable()
            || executableInfo.suffix().compare(QStringLiteral("exe"), Qt::CaseInsensitive) != 0
            || !homeInfo.isAbsolute() || !homeInfo.isDir() || canonicalPath(m_codexHome).isEmpty()) {
            QSKIP("The explicit native executable or isolated Codex home is invalid.");
        }

        const auto defaultHome = QDir::home().filePath(QStringLiteral(".codex"));
        if (QFileInfo(defaultHome).exists() && pathsOverlap(defaultHome, m_codexHome)) {
            QSKIP("The supplied Codex home overlaps the default user Codex home; refusing to use it.");
        }
        QFile marker(QDir(m_codexHome).filePath(QLatin1String(kProfileMarker)));
        if (!marker.open(QIODevice::ReadOnly) || marker.read(128) != QByteArray(kProfileMarkerValue)) {
            QSKIP("The disposable CODEX_HOME sentinel file is missing or invalid.");
        }
        QVERIFY(m_scratch.isValid());

        QProcess versionProbe;
        versionProbe.setProgram(m_executable);
        versionProbe.setArguments({QStringLiteral("--version")});
        versionProbe.setProcessEnvironment(liveProcessEnvironment(m_codexHome));
        versionProbe.setProcessChannelMode(QProcess::SeparateChannels);
        versionProbe.start();
        QVERIFY2(versionProbe.waitForStarted(3000) && versionProbe.waitForFinished(5000),
            "The explicit Codex executable did not answer its bounded --version probe.");
        const auto detectedVersion = CodexVersionSupport::parseVersionResponse(
            QString::fromLocal8Bit(versionProbe.readAllStandardOutput()));
        QVERIFY2(versionProbe.exitCode() == 0 && detectedVersion.has_value(),
            "The explicit Codex executable returned an invalid version response.");
        m_version = *detectedVersion;

        m_process = std::make_unique<CodexProcess>();
        m_client = std::make_unique<CodexClient>(*m_process);
        m_controller = std::make_unique<AssistantController>(*m_client, m_clock, m_ids,
            m_scratch.path(), CodexRuntimeProfile::launchArguments().value(),
            liveProcessEnvironment(m_codexHome), &m_repository);
        m_controller->setPersistenceContext(QLatin1String(kTestAccountScope), true);

        QSignalSpy toolActivity(m_controller.get(), &AssistantController::toolActivityChanged);
        if (CodexVersionSupport::isSupported(m_version)) {
            const QString git = QStandardPaths::findExecutable(QStringLiteral("git"));
            QVERIFY2(!git.isEmpty(), "The live dynamic-tool check requires Git on PATH for its disposable repository.");
            QVERIFY(m_toolWorkspace.isValid());
            QProcess gitInit;
            gitInit.setProgram(git); gitInit.setArguments({QStringLiteral("init"), QStringLiteral("--quiet")});
            gitInit.setWorkingDirectory(m_toolWorkspace.path()); gitInit.start();
            QVERIFY(gitInit.waitForStarted(3000)); QVERIFY(gitInit.waitForFinished(10000)); QCOMPARE(gitInit.exitCode(), 0);

            const auto accountId = devhub::domain::account::AccountId::fromString(
                QStringLiteral("92f343d4-5b1e-41ac-9c83-99f6410e1b3f")).value();
            const auto workspaceId = devhub::domain::workspace::WorkspaceId::fromString(
                QStringLiteral("8a0d03aa-6cd0-46b6-941b-2b362053d97f")).value();
            const QString workspaceRoot = canonicalPath(m_toolWorkspace.path());
            const devhub::domain::workspace::Workspace workspace(workspaceId, accountId,
                QStringLiteral("Disposable live tool workspace"), {m_toolWorkspace.path(), workspaceRoot},
                devhub::domain::workspace::WorkspaceStatus::Ready, false, 0, QDateTime::currentDateTimeUtc());
            m_toolWorkspaceContext.activate(workspace);
            m_controller->setContext({workspaceId.toString(), workspace.displayName(), workspaceRoot});
            m_toolWorkspaceRepository.rows = {workspace};
            m_toolWorkspaceManager = std::make_unique<devhub::application::workspaces::WorkspaceManager>(
                m_toolWorkspaceRepository, m_toolWorkspaceValidator, m_ids, m_clock, accountId);
            m_toolRuntime = std::make_unique<devhub::application::assistant::AssistantToolRuntime>(
                *m_client, *m_controller, m_toolScheduler, *m_toolWorkspaceManager, m_toolWorkspaceContext,
                m_toolServiceRepository, m_toolServiceCoordinator, m_toolNotesRepository);
            QVERIFY(m_toolRuntime->enabledForTestedWireProfile());
        }

        connect(&m_locator, &ICodexExecutableProbe::executableFound, this,
            [this](const CodexExecutable& executable, quint64 generation) {
                m_version = executable.version;
                m_controller->executableFound(executable, generation);
            });
        connect(&m_locator, &ICodexExecutableProbe::probeFailed, this,
            [this](const auto& error, quint64 generation) {
                m_probeFailed = true;
                m_controller->probeFailed(error, generation);
            });
        connect(m_controller.get(), &AssistantController::executableProbeRequested,
            &m_locator, &CodexExecutableLocator::locate);
        connect(m_controller.get(), &AssistantController::cancelProbeRequested,
            &m_locator, &CodexExecutableLocator::cancel);
        connect(m_client.get(), &ICodexClient::profileChecked, this,
            [this](bool accepted, const QStringList& reasons) {
                m_profileChecked = true;
                m_profileAccepted = accepted;
                m_profileReasonCodes = reasons;
            });
        connect(m_client.get(), &ICodexClient::itemChanged, this,
            [this](const CodexItem& item) {
                if (item.type != QStringLiteral("agentMessage") || item.text.isEmpty()) return;
                if (m_stopArmed && !item.started && !m_stopIssued) {
                    m_stopIssued = true;
                    m_controller->stopTurn();
                } else if (!m_stopArmed) {
                    m_streamObserved = true;
                }
            });
        QSignalSpy acceptedTurns(m_client.get(), &ICodexClient::turnAccepted);
        QSignalSpy finishedTurns(m_client.get(), &ICodexClient::turnFinished);

        m_controller->applyConfiguration({m_executable, {}});
        m_controller->open();
        QTRY_VERIFY_WITH_TIMEOUT(m_client->isInitialized() || m_probeFailed, 15000);
        QVERIFY2(!m_probeFailed, "The explicit executable is not in the repository's tested CLI profile set.");
        QTRY_VERIFY_WITH_TIMEOUT(m_profileChecked, 15000);
        QVERIFY2(m_profileAccepted, qPrintable(QStringLiteral("Runtime capability profile rejected: %1")
            .arg(m_profileReasonCodes.join(QLatin1Char(',')))));
        QVERIFY(m_client->isInitialized());
        QVERIFY(m_client->account().mode == CodexAccountMode::ChatGpt);
        QVERIFY(m_client->account().requiresOpenaiAuth);
        QVERIFY(!m_client->account().identityFingerprint.isEmpty());

        // The dedicated library has a separate development-validation policy.
        // The production release gate stays closed and is not modified here.
        QVERIFY2(!CodexRuntimeProfile::liveInferenceApproved(),
            "Developer validation must not change production release approval.");
        QVERIFY2(m_client->canStartConversation(),
            "Live inference requires the explicit build, executable, capability, account, and disposable-home gates.");
        QTRY_VERIFY_WITH_TIMEOUT(m_controller->canSend(), 15000);

        QVERIFY(m_controller->send(QLatin1String(kCompletionPrompt)));
        QTRY_COMPARE_WITH_TIMEOUT(finishedTurns.size(), 1, 90000);
        const auto completed = qvariant_cast<CodexTurn>(finishedTurns.constFirst().at(0));
        QVERIFY(completed.status == CodexTurnStatus::Completed);
        QVERIFY(m_streamObserved);
        QVERIFY(m_repository.hasAssociation());
        QVERIFY(acceptedTurns.size() == 1);
        const auto completedTurnId = acceptedTurns.constFirst().at(1).toString();
        QVERIFY(!completedTurnId.isEmpty());
        const auto completedTurnMessages = countTurnMessages(completedTurnId);
        QVERIFY(completedTurnMessages >= 2);

        m_stopArmed = true;
        QVERIFY(m_controller->send(QLatin1String(kStopPrompt)));
        QTRY_VERIFY_WITH_TIMEOUT(m_stopIssued || finishedTurns.size() > 1, 30000);
        QVERIFY2(m_stopIssued, "The long harmless test turn ended before a streamed delta could trigger Stop.");
        QTRY_COMPARE_WITH_TIMEOUT(finishedTurns.size(), 2, 15000);
        const auto interrupted = qvariant_cast<CodexTurn>(finishedTurns.constLast().at(0));
        QVERIFY(interrupted.status == CodexTurnStatus::Interrupted);
        m_stopArmed = false;

        const auto threadId = m_client->threadBinding().threadId;
        QVERIFY(!threadId.isEmpty());
        m_controller->reconnect();
        QTRY_VERIFY_WITH_TIMEOUT(m_client->isInitialized()
            && m_controller->operation() == AssistantOperation::Idle, 30000);
        const auto restoredKeys = uniqueMessageIdentities();
        QVERIFY(restoredKeys.size() == m_controller->messages().size());
        const auto restoredCompletedKeys = turnIdentityKeys(completedTurnId);
        QVERIFY(restoredCompletedKeys.size() >= 2);
        for (const auto& key : restoredCompletedKeys) QVERIFY(restoredKeys.contains(key));
        QVERIFY(countTurnMessages(completedTurnId) == completedTurnMessages);

        bool liveReadToolPassed = false;
        if (m_toolRuntime) {
            QVERIFY(m_controller->send(QStringLiteral("Use the DevHub read-only tool devhub_git_status exactly once "
                "for the currently selected disposable workspace. Then report only whether it is a Git repository.")));
            QTRY_COMPARE_WITH_TIMEOUT(finishedTurns.size(), 3, 60000);
            QCOMPARE(toolActivity.size(), 2); // one start and one completion proves the tool ran once
            QCOMPARE(toolActivity.constFirst().at(0).toString(), QStringLiteral("git.status"));
            QVERIFY(toolActivity.constFirst().at(1).toBool());
            const auto lastActivity = toolActivity.constLast();
            QCOMPARE(lastActivity.at(0).toString(), QStringLiteral("git.status"));
            QVERIFY(!lastActivity.at(1).toBool());
            QVERIFY(lastActivity.at(2).toBool());
            liveReadToolPassed = true;
        }

        qInfo().noquote() << "DEVHUB_LIVE_RESULT version=" << m_version
                          << " handshake=passed account=chatgpt profile=accepted"
                          << " stream=completed stop=interrupted resume=passed"
                          << " duplicate_history=none read_tool=" << (liveReadToolPassed ? "passed" : "not_in_profile")
                          << " duration_ms=" << m_started.elapsed();
    }

    void cleanup()
    {
        if (!m_controller) return;
        m_toolRuntime.reset();
        bool complete = false;
        m_controller->beginShutdown([&complete] { complete = true; });
        QTRY_VERIFY_WITH_TIMEOUT(complete, 10000);
        m_controller.reset();
        m_client.reset();
        m_process.reset();
    }

private:
    static QProcessEnvironment liveProcessEnvironment(const QString& codexHome)
    {
        auto environment = codexBaseEnvironment(QProcessEnvironment::systemEnvironment());
        environment.insert(QStringLiteral("CODEX_HOME"), codexHome);
        return environment;
    }

    int countTurnMessages(const QString& turnId) const
    {
        int count = 0;
        for (const auto& message : m_controller->messages()) {
            if (message.turnId == turnId) ++count;
        }
        return count;
    }

    QList<QString> turnIdentityKeys(const QString& turnId) const
    {
        QList<QString> keys;
        for (const auto& message : m_controller->messages()) {
            if (message.turnId == turnId) keys.append(identityKey(message));
        }
        return keys;
    }

    QSet<QString> uniqueMessageIdentities() const
    {
        QSet<QString> identities;
        for (const auto& message : m_controller->messages()) identities.insert(identityKey(message));
        return identities;
    }

    QTemporaryDir m_scratch{QDir::current().filePath(QStringLiteral("out/devhub-live-scratch-XXXXXX"))};
    QTemporaryDir m_toolWorkspace{QDir::current().filePath(QStringLiteral("out/devhub-live-tool-workspace-XXXXXX"))};
    CodexExecutableLocator m_locator;
    devhub::shared::time::SystemApplicationClock m_clock;
    devhub::shared::identifiers::UuidIdGenerator m_ids;
    MemoryConversationRepository m_repository;
    LiveWorkspaceRepository m_toolWorkspaceRepository;
    devhub::application::workspaces::WorkspaceValidator m_toolWorkspaceValidator;
    devhub::application::workspaces::WorkspaceContext m_toolWorkspaceContext;
    std::unique_ptr<devhub::application::workspaces::WorkspaceManager> m_toolWorkspaceManager;
    LiveServiceRepository m_toolServiceRepository;
    LiveProcessLauncher m_toolProcessLauncher;
    devhub::application::services::ServiceCoordinator m_toolServiceCoordinator{
        m_toolServiceRepository, m_toolProcessLauncher};
    LiveNotesRepository m_toolNotesRepository;
    devhub::application::tasks::TaskScheduler m_toolScheduler{m_ids};
    std::unique_ptr<devhub::application::assistant::AssistantToolRuntime> m_toolRuntime;
    std::unique_ptr<CodexProcess> m_process;
    std::unique_ptr<CodexClient> m_client;
    std::unique_ptr<AssistantController> m_controller;
    QString m_executable;
    QString m_codexHome;
    QString m_version;
    QStringList m_profileReasonCodes;
    QElapsedTimer m_started;
    bool m_probeFailed = false;
    bool m_profileChecked = false;
    bool m_profileAccepted = false;
    bool m_streamObserved = false;
    bool m_stopArmed = false;
    bool m_stopIssued = false;
};

QTEST_GUILESS_MAIN(AssistantLiveIntegrationTest)
#include "assistant_live_integration_test.moc"
