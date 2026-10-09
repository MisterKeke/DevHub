#pragma once

#include <QProcessEnvironment>
#include <QStringList>
#include <QMetaType>

namespace devhub::application::assistant {

enum class AssistantConnectionState { Disconnected, Probing, Starting, Ready, Stopping, Failed };
enum class AssistantTurnState { Idle, Starting, Running, Cancelling, Completed, Interrupted, Failed };

struct CodexExecutable final {
    const QString path;
    const QString version;
};

// Immutable value snapshots. Copy construction is supported; changing a launch
// or probe result requires constructing a new value rather than editing fields.
struct CodexLaunchSpec final {
    const CodexExecutable executable;
    const QStringList arguments;
    const QString scratchDirectory;
    const QProcessEnvironment environment;
    const quint64 processGeneration = 0;
};

struct CodexTransportLimits final {
    qsizetype recordBytes = 4 * 1024 * 1024;
    qsizetype framingBytes = 8 * 1024 * 1024;
    qint64 stdinBytes = 1024 * 1024;
    qint64 writeChunkBytes = 64 * 1024;
    qsizetype stderrBytes = 64 * 1024;
    int recordsPerDispatch = 64;
    int dispatchMilliseconds = 4;
    int startMilliseconds = 5000;
    int drainMilliseconds = 2000;
    int terminateMilliseconds = 2000;
};

// Pass an explicitly filtered environment. Additional proxy/CA and CODEX_HOME
// policy belongs to the admission layer; tokens and arbitrary variables stay out.
inline QProcessEnvironment codexBaseEnvironment(const QProcessEnvironment& source)
{
    QProcessEnvironment result;
    const QStringList names = {QStringLiteral("SystemRoot"), QStringLiteral("WINDIR"),
        QStringLiteral("COMSPEC"), QStringLiteral("PATH"), QStringLiteral("PATHEXT"),
        QStringLiteral("USERPROFILE"), QStringLiteral("HOMEDRIVE"), QStringLiteral("HOMEPATH"),
        QStringLiteral("LOCALAPPDATA"), QStringLiteral("APPDATA"), QStringLiteral("TEMP"),
        QStringLiteral("TMP"), QStringLiteral("HOME"), QStringLiteral("LANG"), QStringLiteral("LC_ALL")};
    for (const auto& name : names) {
        if (source.contains(name)) result.insert(name, source.value(name));
    }
    return result;
}

} // namespace devhub::application::assistant
Q_DECLARE_METATYPE(devhub::application::assistant::CodexExecutable)
