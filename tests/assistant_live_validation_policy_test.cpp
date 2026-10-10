#include "application/assistant/AssistantTypes.h"
#include "infrastructure/codex/CodexRuntimeProfile.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <QTest>

using namespace devhub::application::assistant;
using namespace devhub::infrastructure::codex;

namespace {
class ScopedEnvironmentVariable final {
public:
    explicit ScopedEnvironmentVariable(QByteArray name)
        : m_name(std::move(name)), m_wasSet(qEnvironmentVariableIsSet(m_name.constData())),
          m_previous(qgetenv(m_name.constData()))
    {
    }

    ~ScopedEnvironmentVariable()
    {
        if (m_wasSet) qputenv(m_name.constData(), m_previous);
        else qunsetenv(m_name.constData());
    }

    void set(const QByteArray& value) { qputenv(m_name.constData(), value); }

private:
    QByteArray m_name;
    bool m_wasSet;
    QByteArray m_previous;
};

bool writeSentinel(const QString& home)
{
    QFile marker(QDir(home).filePath(QStringLiteral(".devhub-disposable-codex-profile")));
    return marker.open(QIODevice::WriteOnly | QIODevice::Truncate)
        && marker.write("DevHub live integration disposable profile\n") >= 0;
}
} // namespace

class AssistantLiveValidationPolicyTest final : public QObject {
    Q_OBJECT

private slots:
    void exactOptInExecutableVersionHomeAndSentinelAreRequired()
    {
        QVERIFY(CodexRuntimeProfile::developmentLiveValidationBuild());
        QVERIFY(!CodexRuntimeProfile::liveInferenceApproved());

        QTemporaryDir home(QDir::current().filePath(QStringLiteral("out/live-validation-home-XXXXXX")));
        QVERIFY(home.isValid());
        QVERIFY(writeSentinel(home.path()));
        const auto executable = QFileInfo(QCoreApplication::applicationFilePath()).canonicalFilePath();
        QVERIFY(!executable.isEmpty());

        ScopedEnvironmentVariable optIn("DEVHUB_RUN_LIVE_ASSISTANT_TESTS");
        ScopedEnvironmentVariable executablePath("DEVHUB_LIVE_CODEX_EXE");
        ScopedEnvironmentVariable disposableHome("DEVHUB_LIVE_CODEX_HOME");
        ScopedEnvironmentVariable disposableAck("DEVHUB_LIVE_DISPOSABLE_PROFILE_ACK");
        optIn.set("I_ACKNOWLEDGE_REAL_CODEX_INFERENCE");
        executablePath.set(executable.toUtf8());
        disposableHome.set(home.path().toUtf8());
        disposableAck.set("I_CREATED_THIS_DISPOSABLE_PROFILE");

        auto childEnvironment = codexBaseEnvironment(QProcessEnvironment::systemEnvironment());
        childEnvironment.insert(QStringLiteral("CODEX_HOME"), home.path());
        QVERIFY(CodexRuntimeProfile::acceptsEnvironment(childEnvironment));
        QCOMPARE(QProcessEnvironment::systemEnvironment().value(QStringLiteral("DEVHUB_RUN_LIVE_ASSISTANT_TESTS")),
            QStringLiteral("I_ACKNOWLEDGE_REAL_CODEX_INFERENCE"));
        QCOMPARE(QProcessEnvironment::systemEnvironment().value(QStringLiteral("DEVHUB_LIVE_CODEX_EXE")), executable);
        QCOMPARE(QProcessEnvironment::systemEnvironment().value(QStringLiteral("DEVHUB_LIVE_CODEX_HOME")), home.path());
        QCOMPARE(QProcessEnvironment::systemEnvironment().value(QStringLiteral("DEVHUB_LIVE_DISPOSABLE_PROFILE_ACK")),
            QStringLiteral("I_CREATED_THIS_DISPOSABLE_PROFILE"));
        QVERIFY(QFileInfo(executable).isAbsolute());
        QVERIFY(QFileInfo(executable).isFile());
        QVERIFY(QFileInfo(executable).isExecutable());
        QVERIFY(QFileInfo(home.path()).isAbsolute());
        QVERIFY(QFileInfo(home.path()).isDir());
        QCOMPARE(QFileInfo(childEnvironment.value(QStringLiteral("CODEX_HOME"))).canonicalFilePath(),
            QFileInfo(home.path()).canonicalFilePath());
        QFile sentinel(QDir(home.path()).filePath(QStringLiteral(".devhub-disposable-codex-profile")));
        QVERIFY(sentinel.open(QIODevice::ReadOnly));
        QCOMPARE(sentinel.read(128), QByteArray("DevHub live integration disposable profile\n"));
        sentinel.close();
        const auto authorized = [&] {
            return CodexRuntimeProfile::developmentLiveInferenceAuthorized(
                executable, QStringLiteral("0.160.1"), childEnvironment);
        };

        QVERIFY(authorized());
        optIn.set("no");
        QVERIFY(!authorized());
        optIn.set("I_ACKNOWLEDGE_REAL_CODEX_INFERENCE");
        disposableAck.set("no");
        QVERIFY(!authorized());
        disposableAck.set("I_CREATED_THIS_DISPOSABLE_PROFILE");
        QVERIFY(!CodexRuntimeProfile::developmentLiveInferenceAuthorized(
            executable, QStringLiteral("0.160.2"), childEnvironment));
        QVERIFY(!CodexRuntimeProfile::developmentLiveInferenceAuthorized(
            QDir(home.path()).filePath(QStringLiteral("other.exe")), QStringLiteral("0.160.1"), childEnvironment));
        childEnvironment.insert(QStringLiteral("CODEX_HOME"), QDir(home.path()).filePath(QStringLiteral("other")));
        QVERIFY(!authorized());
        childEnvironment.insert(QStringLiteral("CODEX_HOME"), home.path());
        QFile::remove(QDir(home.path()).filePath(QStringLiteral(".devhub-disposable-codex-profile")));
        QVERIFY(!authorized());
    }
};

QTEST_GUILESS_MAIN(AssistantLiveValidationPolicyTest)
#include "assistant_live_validation_policy_test.moc"
