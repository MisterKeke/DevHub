#pragma once

#include "../tasks/Cancellation.h"

#include <QByteArray>
#include <QDateTime>
#include <QString>
#include <QStringList>
#include <QVector>

#include <chrono>

namespace devhub::application::git {

struct GitVersion final {
    int major = 0; int minor = 0; int patch = 0; QString raw;
    [[nodiscard]] bool supported() const noexcept { return major > 2 || (major == 2 && minor >= 40); }
    friend bool operator<(const GitVersion& a, const GitVersion& b) noexcept {
        return a.major != b.major ? a.major < b.major : a.minor != b.minor ? a.minor < b.minor : a.patch < b.patch;
    }
};

struct GitExecutable final { QString path; GitVersion version; };
struct GitCommand final {
    QString operation;
    QStringList arguments;
    QString directory;
    std::chrono::milliseconds timeout{10000};
    bool mutation = false;
    bool diffPreview = false;
};
struct GitCommandResult final {
    QByteArray standardOutput;
    QByteArray standardError;
    int exitCode = -1;
    int exitStatus = 0;
    qint64 durationMs = 0;
    bool outputTruncated = false;
    bool processStarted = false;
    bool timedOut = false;
    bool cancelled = false;
    QString diagnostic;
};
enum class GitFileKind { Ordinary, Rename, Copy, Unmerged, Untracked };
struct GitFileStatus final {
    QByteArray pathBytes;
    QByteArray originalPathBytes;
    QString path;
    QString originalPath;
    QChar indexStatus = QLatin1Char(' ');
    QChar workTreeStatus = QLatin1Char(' ');
    GitFileKind kind = GitFileKind::Ordinary;
};
struct GitBranchState final {
    QString name;
    QString objectId;
    QString upstream;
    int ahead = 0;
    int behind = 0;
    bool current = false;
    bool remote = false;
};
struct GitRepositoryState final {
    bool isRepository = false;
    bool detached = false;
    QString root;
    QString gitDirectory;
    QString commonDirectory;
    QString branch;
    QString objectId;
    QString upstream;
    int ahead = 0;
    int behind = 0;
    QVector<GitFileStatus> files;
    QDateTime fetchedAt;
};
struct GitCommit final {
    QString objectId;
    QString abbreviatedObjectId;
    QString subject;
    QString author;
    QString email;
    QDateTime authoredAt;
};
struct GitRemote final { QString name; QString fetchUrl; QString pushUrl; };
struct GitDiff final { QByteArray bytes; bool binary = false; bool truncated = false; };

// Immutable approval snapshot for one normal branch publication. The raw
// configuration fingerprint is internal and must never be serialized to tools.
struct GitPushPreview final {
    QString repositoryRoot;
    QString repositoryIdentity;
    QString localBranch;
    QString sourceCommit;
    QString upstream;
    QString remote;
    QString effectivePushUrl;
    // Kept only in the in-memory approved operation; never serialize this field.
    QString pushUrlForExecution;
    QString targetBranch;
    QString remoteTrackingCommit;
    QString trackingDescription;
    QString remoteState = QStringLiteral("notQueried");
    QVector<GitCommit> outgoingCommits;
    int outgoingCommitCount = -1;
    bool outgoingCommitsTruncated = false;
    bool outgoingCountAvailable = false;
    bool hasCachedRemoteTracking = false;
    bool establishUpstream = false;
    bool upstreamChange = false;
    QByteArray configurationFingerprint;
};

enum class GitPushOutcome {
    Published,
    AlreadyUpToDate,
    AuthenticationRequired,
    NonFastForward,
    HookRejected,
    RemoteRejected,
    TimedOut,
    Cancelled,
    PartialTrackingFailure,
    Uncertain,
    Failed,
    PreviewChanged
};

struct GitPushResult final {
    GitPushOutcome outcome = GitPushOutcome::Failed;
    bool remotePublished = false;
    bool trackingConfigured = false;
    bool completionUncertain = false;
    QString message;
    QString diagnostic;
    QString approvedSourceCommit;
    QString remote;
    QString targetBranch;
};

} // namespace devhub::application::git
