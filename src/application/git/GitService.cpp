#include "GitService.h"
#include "GitStatusParser.h"
#include "GitRepositoryLocator.h"
#include "../../shared/security/RedactionService.h"
#include <QDateTime>
#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QMutex>
#include <QMutexLocker>
#include <memory>
#include <mutex>
#include <thread>

namespace devhub::application::git {
namespace {
std::mutex& mutationLockFor(const QString& directory) {
    static QMutex guard;
    static QHash<QString, std::shared_ptr<std::mutex>> lockByPath;
    QString canonical = QFileInfo(directory).canonicalFilePath();
    if (canonical.isEmpty()) canonical = QDir::cleanPath(QFileInfo(directory).absoluteFilePath());
    const QString key = QDir::cleanPath(canonical).toCaseFolded();
    QMutexLocker lock(&guard);
    auto& repositoryLock = lockByPath[key];
    if (!repositoryLock) repositoryLock = std::make_shared<std::mutex>();
    return *repositoryLock;
}
bool acquireMutationLock(std::unique_lock<std::mutex>& lock, const tasks::CancellationToken& cancellation) {
    while (!lock.try_lock()) {
        if (cancellation.isCancellationRequested()) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return !cancellation.isCancellationRequested();
}
domain::errors::AppError commandError(const GitCommandResult& result, const QString& operation) {
    QString technical = QString::fromUtf8(result.standardError).trimmed();
    if (result.timedOut) technical = QStringLiteral("Git command timed out.");
    if (result.outputTruncated) technical += QStringLiteral(" Output was truncated.");
    shared::security::RedactionService redactor;
    return domain::errors::AppError::failure(domain::errors::ErrorCode::InternalFailure,
        domain::errors::ErrorCategory::Git, QStringLiteral("Git could not complete %1.").arg(operation),
        redactor.redact(technical), operation);
}
domain::errors::AppError invalidGitInput(const QString& message, const QString& operation) {
    return domain::errors::AppError::failure(domain::errors::ErrorCode::InvalidArgument,
        domain::errors::ErrorCategory::Git, message, QStringLiteral("Rejected an invalid Git ref, remote, or selector."), operation);
}
bool safeTextArgument(const QString& value) {
    if (value.isEmpty() || value != value.trimmed() || value.startsWith(QLatin1Char('-'))) return false;
    for (const QChar character : value) if (character.isNull() || character.category() == QChar::Other_Control) return false;
    return true;
}
bool validBranchName(const GitCommandRunner& runner, const QString& directory, const QString& value,
                     const tasks::CancellationToken& cancellation) {
    if (!safeTextArgument(value)) return false;
    const GitCommand check{QStringLiteral("validate-branch"),
        {QStringLiteral("check-ref-format"), QStringLiteral("refs/heads/") + value}, directory};
    return runner.run(check, cancellation).exitCode == 0;
}
bool validRevision(const GitCommandRunner& runner, const QString& directory, const QString& value,
                   const tasks::CancellationToken& cancellation) {
    if (!safeTextArgument(value)) return false;
    const GitCommand check{QStringLiteral("validate-revision"),
        {QStringLiteral("rev-parse"), QStringLiteral("--verify"), QStringLiteral("--end-of-options"),
         value + QStringLiteral("^{commit}")}, directory};
    return runner.run(check, cancellation).exitCode == 0;
}
bool validRemote(const GitCommandRunner& runner, const QString& directory, const QString& value,
                 const tasks::CancellationToken& cancellation) {
    if (!safeTextArgument(value)) return false;
    const GitCommand list{QStringLiteral("validate-remote"), {QStringLiteral("remote")}, directory};
    const auto result = runner.run(list, cancellation);
    if (result.exitCode != 0) return false;
    for (const QByteArray& line : result.standardOutput.split('\n'))
        if (QString::fromUtf8(line).trimmed() == value) return true;
    return false;
}
bool validStashSelector(const QString& value) {
    static const QRegularExpression selector(QStringLiteral("^stash@\\{[0-9]+\\}$|^[0-9a-fA-F]{40,64}$"));
    return safeTextArgument(value) && selector.match(value).hasMatch();
}
QString configValue(const GitCommandRunner& runner, const QString& directory, const QString& key,
                    const tasks::CancellationToken& cancellation) {
    const GitCommand query{QStringLiteral("push-config"), {QStringLiteral("config"), QStringLiteral("--get"), key}, directory};
    const auto result = runner.run(query, cancellation);
    return result.exitCode == 0 ? QString::fromUtf8(result.standardOutput).trimmed() : QString{};
}
bool gitBoolean(const QString& value) {
    const QString normalized = value.trimmed().toLower();
    return normalized == QStringLiteral("true") || normalized == QStringLiteral("yes")
        || normalized == QStringLiteral("on") || normalized == QStringLiteral("1");
}
QByteArray configurationFingerprint(const GitCommandRunner& runner, const QString& directory,
                                    const tasks::CancellationToken& cancellation) {
    const GitCommand query{QStringLiteral("push-config-fingerprint"),
        {QStringLiteral("config"), QStringLiteral("--null"), QStringLiteral("--list")}, directory};
    const auto result = runner.run(query, cancellation);
    if (result.exitCode != 0 || result.timedOut || result.cancelled || result.outputTruncated) return {};
    return QCryptographicHash::hash(result.standardOutput, QCryptographicHash::Sha256);
}
bool samePushApproval(const GitPushPreview& expected, const GitPushPreview& current) {
    return expected.repositoryRoot == current.repositoryRoot
        && expected.repositoryIdentity == current.repositoryIdentity
        && expected.localBranch == current.localBranch
        && expected.sourceCommit == current.sourceCommit
        && expected.upstream == current.upstream
        && expected.remote == current.remote
        && expected.effectivePushUrl == current.effectivePushUrl
        && expected.pushUrlForExecution == current.pushUrlForExecution
        && expected.targetBranch == current.targetBranch
        && expected.remoteTrackingCommit == current.remoteTrackingCommit
        && expected.establishUpstream == current.establishUpstream
        && expected.upstreamChange == current.upstreamChange
        && expected.configurationFingerprint == current.configurationFingerprint;
}
QString boundedDiagnostic(const GitCommandResult& result) {
    shared::security::RedactionService redactor;
    QString detail = QString::fromUtf8(result.standardError).trimmed();
    if (detail.isEmpty()) detail = QString::fromUtf8(result.standardOutput).trimmed();
    if (detail.isEmpty()) detail = result.diagnostic.trimmed();
    if (result.outputTruncated) detail += QStringLiteral("\n[Git diagnostic output was truncated]");
    detail = redactor.redact(detail);
    if (detail.size() > 2000) detail = detail.left(2000) + QStringLiteral("…");
    return detail;
}
}
shared::common::Result<GitRepositoryState> GitService::status(const QString& directory,
    const tasks::CancellationToken& cancellation) const {
    GitRepositoryLocator locator(m_runner);
    auto repository = locator.locate(directory, cancellation);
    if (!repository) return repository;
    if (!repository.value().isRepository) return repository;
    GitCommand command{QStringLiteral("status"), {QStringLiteral("status"), QStringLiteral("--porcelain=v2"),
        QStringLiteral("--branch"), QStringLiteral("-z")}, repository.value().root, std::chrono::milliseconds(5000)};
    auto result = m_runner.run(command, cancellation);
    if (result.exitCode != 0 || result.timedOut || result.cancelled)
        return shared::common::Result<GitRepositoryState>::failure(commandError(result, command.operation));
    auto state = GitStatusParser::parse(result.standardOutput); state.root = repository.value().root;
    state.fetchedAt = QDateTime::currentDateTimeUtc();
    return shared::common::Result<GitRepositoryState>::success(std::move(state));
}
shared::common::Result<QVector<GitBranchState>> GitService::branches(const QString& directory, bool remote,
    const tasks::CancellationToken& cancellation) const {
    const QString format = QStringLiteral("%(refname:short)%00%(objectname)%00%(HEAD)%00%(upstream:short)%00%(upstream:track)");
    GitCommand command{QStringLiteral("branch-list"), {QStringLiteral("for-each-ref"), QStringLiteral("--format=") + format,
        remote ? QStringLiteral("refs/remotes/") : QStringLiteral("refs/heads/")}, directory};
    auto result = m_runner.run(command, cancellation);
    if (result.exitCode != 0) return shared::common::Result<QVector<GitBranchState>>::failure(commandError(result, command.operation));
    QVector<GitBranchState> branches;
    for (const QByteArray& row : result.standardOutput.split('\n')) {
        if (row.trimmed().isEmpty()) continue;
        const auto fields = row.split('\0'); if (fields.size() < 5) continue;
        GitBranchState branch; branch.name = QString::fromUtf8(fields[0]); branch.objectId = QString::fromLatin1(fields[1]);
        branch.current = fields[2] == QByteArray("*"); branch.upstream = QString::fromUtf8(fields[3]); branch.remote = remote;
        const QByteArray track = fields[4];
        QRegularExpression ahead(QStringLiteral("ahead (\\d+)")), behind(QStringLiteral("behind (\\d+)"));
        auto a = ahead.match(QString::fromUtf8(track)); auto b = behind.match(QString::fromUtf8(track));
        if (a.hasMatch()) branch.ahead = a.captured(1).toInt();
        if (b.hasMatch()) branch.behind = b.captured(1).toInt();
        branches.append(std::move(branch));
    }
    return shared::common::Result<QVector<GitBranchState>>::success(std::move(branches));
}
shared::common::Result<QVector<GitRemote>> GitService::remotes(const QString& directory,
    const tasks::CancellationToken& cancellation) const {
    GitCommand command{QStringLiteral("remote-list"), {QStringLiteral("remote")}, directory};
    auto result = m_runner.run(command, cancellation);
    if (result.exitCode != 0) return shared::common::Result<QVector<GitRemote>>::failure(commandError(result, command.operation));
    QVector<GitRemote> remotes; shared::security::RedactionService redactor;
    for (const QByteArray& row : result.standardOutput.split('\n')) {
        const QString name = QString::fromUtf8(row).trimmed(); if (name.isEmpty()) continue;
        GitRemote remote; remote.name = name;
        for (const auto& pair : {qMakePair(QStringLiteral("fetch"), &remote.fetchUrl), qMakePair(QStringLiteral("push"), &remote.pushUrl)}) {
            GitCommand urlCommand{QStringLiteral("remote-url"), {QStringLiteral("remote"), QStringLiteral("get-url"),
                pair.first == QStringLiteral("push") ? QStringLiteral("--push") : QStringLiteral("--all"), QStringLiteral("--"), name}, directory};
            auto urlResult = m_runner.run(urlCommand, cancellation);
            if (urlResult.exitCode == 0) *pair.second = redactor.redact(QString::fromUtf8(urlResult.standardOutput).trimmed());
        }
        remotes.append(std::move(remote));
    }
    return shared::common::Result<QVector<GitRemote>>::success(std::move(remotes));
}
shared::common::Result<QVector<GitCommit>> GitService::history(const QString& directory, int offset, int limit,
    const tasks::CancellationToken& cancellation) const {
    const QString format = QStringLiteral("%H%x00%h%x00%s%x00%an%x00%ae%x00%aI%x1e");
    GitCommand command{QStringLiteral("history"), {QStringLiteral("log"), QStringLiteral("--all"), QStringLiteral("--date=iso-strict"),
        QStringLiteral("--format=") + format, QStringLiteral("--skip=%1").arg(qMax(0, offset)),
        QStringLiteral("-n%1").arg(qBound(1, limit, 200))}, directory};
    auto result = m_runner.run(command, cancellation);
    if (result.exitCode != 0) return shared::common::Result<QVector<GitCommit>>::failure(commandError(result, command.operation));
    QVector<GitCommit> commits;
    for (QByteArray record : result.standardOutput.split(0x1e)) {
        // Git terminates each formatted log entry with a newline after our record separator.
        // Without removing it, every object ID after the first starts with '\n'.
        if (record.startsWith("\r\n")) record.remove(0, 2);
        else if (record.startsWith('\n')) record.remove(0, 1);
        if (record.trimmed().isEmpty()) continue;
        const auto fields = record.split('\0'); if (fields.size() < 6) continue;
        GitCommit commit; commit.objectId = QString::fromLatin1(fields[0]); commit.abbreviatedObjectId = QString::fromLatin1(fields[1]);
        commit.subject = QString::fromUtf8(fields[2]); commit.author = QString::fromUtf8(fields[3]); commit.email = QString::fromUtf8(fields[4]);
        commit.authoredAt = QDateTime::fromString(QString::fromUtf8(fields[5]), Qt::ISODateWithMs);
        if (!commit.authoredAt.isValid()) commit.authoredAt = QDateTime::fromString(QString::fromUtf8(fields[5]), Qt::ISODate);
        commits.append(std::move(commit));
    }
    return shared::common::Result<QVector<GitCommit>>::success(std::move(commits));
}
shared::common::Result<GitDiff> GitService::diff(const QString& directory, bool staged, const QString& commit,
    const tasks::CancellationToken& cancellation) const {
    QStringList args{QStringLiteral("diff"), QStringLiteral("--no-ext-diff"), QStringLiteral("--binary"), QStringLiteral("--")};
    if (!commit.isEmpty()) {
        if (!validRevision(m_runner, directory, commit, cancellation))
            return shared::common::Result<GitDiff>::failure(
                invalidGitInput(QStringLiteral("Choose a valid commit to inspect."), QStringLiteral("diff preview")));
        args = {QStringLiteral("show"), QStringLiteral("--format="), QStringLiteral("--no-ext-diff"), commit};
    }
    else if (staged) args = {QStringLiteral("diff"), QStringLiteral("--cached"), QStringLiteral("--no-ext-diff"), QStringLiteral("--binary")};
    else args = {QStringLiteral("diff"), QStringLiteral("--no-ext-diff"), QStringLiteral("--binary")};
    GitCommand command{QStringLiteral("diff-preview"), args, directory, std::chrono::milliseconds(15000), false, true};
    auto result = m_runner.run(command, cancellation);
    if (result.exitCode != 0) return shared::common::Result<GitDiff>::failure(commandError(result, command.operation));
    GitDiff diff; diff.bytes = std::move(result.standardOutput); diff.truncated = result.outputTruncated;
    diff.binary = diff.bytes.contains("GIT binary patch") || diff.bytes.contains("Binary files ");
    return shared::common::Result<GitDiff>::success(std::move(diff));
}
shared::common::Result<GitCommandResult> GitService::mutate(const QString& directory, QString operation,
    QStringList arguments, const tasks::CancellationToken& cancellation) const {
    GitRepositoryLocator locator(m_runner);
    const auto before = locator.locate(directory, cancellation);
    if (!before || !before.value().isRepository || before.value().commonDirectory.isEmpty())
        return shared::common::Result<GitCommandResult>::failure(before ? invalidGitInput(
            QStringLiteral("Open a Git repository before changing it."), operation) : before.error());
    std::unique_lock<std::mutex> repositoryLock(mutationLockFor(before.value().commonDirectory), std::defer_lock);
    if (!acquireMutationLock(repositoryLock, cancellation)) return shared::common::Result<GitCommandResult>::failure(
        domain::errors::AppError::failure(domain::errors::ErrorCode::OperationCancelled,
            domain::errors::ErrorCategory::Internal, QStringLiteral("Git mutation was cancelled while waiting for the repository."),
            {}, operation));
    const auto after = locator.locate(directory, cancellation);
    if (!after || !after.value().isRepository || after.value().commonDirectory.isEmpty()
        || QDir::cleanPath(after.value().commonDirectory).compare(
            QDir::cleanPath(before.value().commonDirectory), Qt::CaseInsensitive) != 0)
        return shared::common::Result<GitCommandResult>::failure(invalidGitInput(
            QStringLiteral("The repository changed while this Git operation was waiting. Refresh and try again."), operation));
    GitCommand command{operation, std::move(arguments), directory, std::chrono::milliseconds(120000), true};
    auto result = m_runner.run(command, cancellation);
    if (result.exitCode != 0 || result.timedOut || result.cancelled)
        return shared::common::Result<GitCommandResult>::failure(commandError(result, operation));
    qInfo().noquote() << QStringLiteral("Git mutation completed: operation=%1 durationMs=%2")
        .arg(operation).arg(result.durationMs);
    return shared::common::Result<GitCommandResult>::success(std::move(result));
}
shared::common::Result<GitCommandResult> GitService::stage(const QString& directory, const QStringList& paths,
    const tasks::CancellationToken& cancellation) const {
    if (paths.isEmpty()) return shared::common::Result<GitCommandResult>::failure(
        domain::errors::AppError::failure(domain::errors::ErrorCode::InvalidArgument,
            domain::errors::ErrorCategory::Git, QStringLiteral("Select at least one file to stage."),
            QStringLiteral("Empty path list rejected."), QStringLiteral("stage files")));
    QStringList args{QStringLiteral("add"), QStringLiteral("--")}; args.append(paths);
    return mutate(directory, QStringLiteral("stage"), std::move(args), cancellation);
}
shared::common::Result<GitCommandResult> GitService::stageAll(const QString& directory,
    const tasks::CancellationToken& cancellation) const {
    return mutate(directory, QStringLiteral("stage-all"),
        {QStringLiteral("add"), QStringLiteral("--all")}, cancellation);
}
shared::common::Result<GitCommandResult> GitService::unstage(const QString& directory, const QStringList& paths,
    const tasks::CancellationToken& cancellation) const {
    if (paths.isEmpty()) return shared::common::Result<GitCommandResult>::failure(
        domain::errors::AppError::failure(domain::errors::ErrorCode::InvalidArgument,
            domain::errors::ErrorCategory::Git, QStringLiteral("Select at least one file to unstage."),
            QStringLiteral("Empty path list rejected."), QStringLiteral("unstage files")));
    QStringList args{QStringLiteral("restore"), QStringLiteral("--staged"), QStringLiteral("--")}; args.append(paths);
    return mutate(directory, QStringLiteral("unstage"), std::move(args), cancellation);
}
shared::common::Result<GitCommandResult> GitService::commit(const QString& directory, const QString& summary,
    const tasks::CancellationToken& cancellation) const {
    const QString message = summary.trimmed();
    if (message.isEmpty() || message.size() > 200) return shared::common::Result<GitCommandResult>::failure(
        domain::errors::AppError::failure(domain::errors::ErrorCode::InvalidArgument,
            domain::errors::ErrorCategory::Git, QStringLiteral("Enter a commit summary between 1 and 200 characters."),
            QStringLiteral("Commit summary validation failed."), QStringLiteral("commit")));
    return mutate(directory, QStringLiteral("commit"), {QStringLiteral("commit"), QStringLiteral("-m"), message}, cancellation);
}
shared::common::Result<GitCommandResult> GitService::fetch(const QString& directory, const QString& remote,
    const tasks::CancellationToken& cancellation) const {
    if (!validRemote(m_runner, directory, remote, cancellation)) return shared::common::Result<GitCommandResult>::failure(
        invalidGitInput(QStringLiteral("Choose a valid configured remote to fetch."), QStringLiteral("fetch")));
    return mutate(directory, QStringLiteral("fetch"), {QStringLiteral("fetch"), QStringLiteral("--"), remote}, cancellation);
}
shared::common::Result<GitCommandResult> GitService::pull(const QString& directory, const QString& remote,
    const QString& branch, const QString& explicitStrategy, const tasks::CancellationToken& cancellation) const {
    if (!validRemote(m_runner, directory, remote, cancellation)
        || !validBranchName(m_runner, directory, branch, cancellation))
        return shared::common::Result<GitCommandResult>::failure(
            invalidGitInput(QStringLiteral("Choose a valid configured remote and branch to pull."), QStringLiteral("pull")));
    QString strategy = explicitStrategy;
    if (strategy.isEmpty()) {
        GitCommand query{QStringLiteral("pull-strategy"), {QStringLiteral("config"), QStringLiteral("--get"), QStringLiteral("pull.rebase")}, directory};
        const auto configured = m_runner.run(query, cancellation);
        if (configured.exitCode == 0) {
            const QString value = QString::fromUtf8(configured.standardOutput).trimmed();
            strategy = (value == QStringLiteral("true") || value == QStringLiteral("1") || value == QStringLiteral("merges"))
                ? QStringLiteral("rebase") : QStringLiteral("merge");
        } else {
            query.arguments = {QStringLiteral("config"), QStringLiteral("--get"), QStringLiteral("pull.ff")};
            const auto ff = m_runner.run(query, cancellation);
            if (ff.exitCode == 0 && QString::fromUtf8(ff.standardOutput).trimmed() == QStringLiteral("only"))
                strategy = QStringLiteral("ff-only");
        }
        if (strategy.isEmpty()) return shared::common::Result<GitCommandResult>::failure(
            domain::errors::AppError::failure(domain::errors::ErrorCode::InvalidArgument, domain::errors::ErrorCategory::Git,
                QStringLiteral("Choose a pull strategy: merge, rebase, or fast-forward only."),
                QStringLiteral("Repository pull strategy is unspecified."), QStringLiteral("pull")));
    }
    QStringList args{QStringLiteral("pull")};
    if (strategy == QStringLiteral("merge")) args.append(QStringLiteral("--no-rebase"));
    else if (strategy == QStringLiteral("rebase")) args.append(QStringLiteral("--rebase"));
    else if (strategy == QStringLiteral("ff-only")) args.append(QStringLiteral("--ff-only"));
    else return shared::common::Result<GitCommandResult>::failure(domain::errors::AppError::failure(
        domain::errors::ErrorCode::InvalidArgument, domain::errors::ErrorCategory::Git,
        QStringLiteral("Unsupported pull strategy."), strategy, QStringLiteral("pull")));
    args << QStringLiteral("--") << remote << (QStringLiteral("refs/heads/") + branch);
    return mutate(directory, QStringLiteral("pull"), std::move(args), cancellation);
}
shared::common::Result<QStringList> GitService::configuredRemoteNames(const QString& directory,
    const tasks::CancellationToken& cancellation) const {
    GitCommand command{QStringLiteral("remote-list"), {QStringLiteral("remote")}, directory};
    const auto result = m_runner.run(command, cancellation);
    if (result.exitCode != 0 || result.timedOut || result.cancelled)
        return shared::common::Result<QStringList>::failure(commandError(result, command.operation));
    QStringList remotes;
    for (const auto& row : result.standardOutput.split('\n')) {
        const QString name = QString::fromUtf8(row).trimmed();
        if (!name.isEmpty()) remotes.append(name);
    }
    return shared::common::Result<QStringList>::success(std::move(remotes));
}

shared::common::Result<GitPushPreview> GitService::preparePush(const QString& directory,
    const QString& requestedRemote, const QString& requestedTargetBranch, bool establishUpstream,
    const tasks::CancellationToken& cancellation) const {
    GitRepositoryLocator locator(m_runner);
    const auto repository = locator.locate(directory, cancellation);
    if (!repository || !repository.value().isRepository)
        return shared::common::Result<GitPushPreview>::failure(repository ? invalidGitInput(
            QStringLiteral("Open a Git repository before pushing."), QStringLiteral("prepare push")) : repository.error());
    if (repository.value().root.isEmpty() || repository.value().commonDirectory.isEmpty())
        return shared::common::Result<GitPushPreview>::failure(invalidGitInput(
            QStringLiteral("Git could not resolve this repository's canonical identity."), QStringLiteral("prepare push")));

    const QString root = repository.value().root;
    QString selectedWorkspace = QFileInfo(directory).canonicalFilePath();
    if (selectedWorkspace.isEmpty()) selectedWorkspace = QDir::cleanPath(QFileInfo(directory).absoluteFilePath());
    if (QDir::cleanPath(selectedWorkspace).compare(QDir::cleanPath(root), Qt::CaseInsensitive) != 0)
        return shared::common::Result<GitPushPreview>::failure(invalidGitInput(
            QStringLiteral("For push safety, select the trusted workspace at the Git repository root."), QStringLiteral("prepare push")));
    auto fail = [](const QString& message) {
        return shared::common::Result<GitPushPreview>::failure(invalidGitInput(message, QStringLiteral("prepare push")));
    };
    GitCommand branchCommand{QStringLiteral("push-source-branch"),
        {QStringLiteral("symbolic-ref"), QStringLiteral("--quiet"), QStringLiteral("--short"), QStringLiteral("HEAD")}, root};
    const auto branchResult = m_runner.run(branchCommand, cancellation);
    if (branchResult.cancelled) return shared::common::Result<GitPushPreview>::failure(commandError(branchResult, branchCommand.operation));
    if (branchResult.exitCode != 0) return fail(QStringLiteral("This branch is in detached HEAD state. Check out a branch before publishing it."));
    const QString localBranch = QString::fromUtf8(branchResult.standardOutput).trimmed();
    if (!validBranchName(m_runner, root, localBranch, cancellation))
        return fail(QStringLiteral("Git could not validate the current branch. Refresh the repository and try again."));

    GitCommand commitCommand{QStringLiteral("push-source-commit"),
        {QStringLiteral("rev-parse"), QStringLiteral("--verify"), QStringLiteral("--end-of-options"),
         QStringLiteral("HEAD^{commit}")}, root};
    const auto commitResult = m_runner.run(commitCommand, cancellation);
    const QString sourceCommit = QString::fromLatin1(commitResult.standardOutput).trimmed();
    static const QRegularExpression fullObjectId(QStringLiteral("^(?:[0-9a-fA-F]{40}|[0-9a-fA-F]{64})$"));
    if (commitResult.exitCode != 0 || !fullObjectId.match(sourceCommit).hasMatch())
        return fail(QStringLiteral("This branch has no commit yet. Create a commit before publishing the branch."));

    const QString branchConfig = QStringLiteral("branch.%1.").arg(localBranch);
    const QString configuredUpstreamRemote = configValue(m_runner, root, branchConfig + QStringLiteral("remote"), cancellation);
    const QString configuredUpstreamRef = configValue(m_runner, root, branchConfig + QStringLiteral("merge"), cancellation);
    QString upstream;
    QString inferredRemote;
    QString inferredTarget;
    if (!configuredUpstreamRemote.isEmpty() && !configuredUpstreamRef.isEmpty()) {
        const QString prefix = QStringLiteral("refs/heads/");
        if (configuredUpstreamRef.startsWith(prefix)) {
            inferredRemote = configuredUpstreamRemote;
            inferredTarget = configuredUpstreamRef.mid(prefix.size());
            upstream = inferredRemote == QStringLiteral(".") ? inferredTarget
                : inferredRemote + QLatin1Char('/') + inferredTarget;
        }
    }
    const QString remote = requestedRemote.trimmed().isEmpty() ? inferredRemote : requestedRemote;
    const QString targetBranch = requestedTargetBranch.trimmed().isEmpty() ? inferredTarget : requestedTargetBranch;
    if (remote.isEmpty() || remote == QStringLiteral("."))
        return fail(QStringLiteral("Choose a configured remote for this branch. DevHub will not assume a remote."));
    if (targetBranch.isEmpty())
        return fail(QStringLiteral("Choose a target branch on the configured remote. DevHub will not assume a matching name."));
    if (!validRemote(m_runner, root, remote, cancellation)
        || !validBranchName(m_runner, root, targetBranch, cancellation))
        return fail(QStringLiteral("Choose a valid configured remote and target branch."));

    GitCommand urlsCommand{QStringLiteral("push-destination"),
        {QStringLiteral("remote"), QStringLiteral("get-url"), QStringLiteral("--push"), QStringLiteral("--all"), remote}, root};
    const auto urlsResult = m_runner.run(urlsCommand, cancellation);
    if (urlsResult.exitCode != 0 || urlsResult.timedOut || urlsResult.cancelled)
        return shared::common::Result<GitPushPreview>::failure(commandError(urlsResult, urlsCommand.operation));
    QStringList effectiveUrls;
    for (const QByteArray& line : urlsResult.standardOutput.split('\n')) {
        const QString url = QString::fromUtf8(line).trimmed();
        if (!url.isEmpty()) effectiveUrls.append(url);
    }
    if (effectiveUrls.size() != 1)
        return fail(effectiveUrls.isEmpty()
            ? QStringLiteral("This remote has no effective push URL. Configure one before publishing.")
            : QStringLiteral("This remote has multiple push destinations. DevHub supports one destination per push."));

    const QString remoteKey = QStringLiteral("remote.%1.").arg(remote);
    const QString mirror = configValue(m_runner, root, remoteKey + QStringLiteral("mirror"), cancellation);
    if (gitBoolean(mirror)) return fail(QStringLiteral("Mirror remotes can publish or delete multiple refs. Disable mirror mode before using DevHub push."));
    GitCommand customPushCommand{QStringLiteral("push-configured-refspec"),
        {QStringLiteral("config"), QStringLiteral("--null"), QStringLiteral("--get-all"), remoteKey + QStringLiteral("push")}, root};
    const auto customPush = m_runner.run(customPushCommand, cancellation);
    if (customPush.exitCode == 0 && !customPush.standardOutput.isEmpty())
        return fail(QStringLiteral("This remote has custom push refspecs. Remove them before using the single-branch push workflow."));
    const QString followTags = configValue(m_runner, root, QStringLiteral("push.followTags"), cancellation);
    if (gitBoolean(followTags)) return fail(QStringLiteral("Automatic tag pushing is enabled. Disable push.followTags before using the single-branch workflow."));
    const QString recurse = configValue(m_runner, root, QStringLiteral("push.recurseSubmodules"), cancellation);
    if (!recurse.isEmpty() && recurse.compare(QStringLiteral("no"), Qt::CaseInsensitive) != 0)
        return fail(QStringLiteral("Recursive submodule pushing is configured. Set push.recurseSubmodules to no before publishing one branch."));

    for (const QString& key : {QStringLiteral("push.pushOption"), remoteKey + QStringLiteral("pushOption"),
                               remoteKey + QStringLiteral("proxy"), remoteKey + QStringLiteral("receivepack")}) {
        GitCommand optionCommand{QStringLiteral("push-remote-option"),
            {QStringLiteral("config"), QStringLiteral("--null"), QStringLiteral("--get-all"), key}, root};
        const auto configured = m_runner.run(optionCommand, cancellation);
        if (configured.exitCode == 0 && !configured.standardOutput.isEmpty())
            return fail(QStringLiteral("This repository has push options or remote-specific transport settings that cannot be preserved while pinning the approved destination. Review the remote configuration first."));
    }

    // When using the resolved URL directly, reject a second rewrite that would
    // make the actual command address a different destination than the preview.
    GitCommand rewriteCommand{QStringLiteral("push-url-rewrites"),
        {QStringLiteral("config"), QStringLiteral("--null"), QStringLiteral("--get-regexp"),
         QStringLiteral("^url\\..*\\.(insteadof|pushinsteadof)$")}, root};
    const auto rewrites = m_runner.run(rewriteCommand, cancellation);
    if (rewrites.exitCode == 0) {
        for (const QByteArray& row : rewrites.standardOutput.split('\0')) {
            const qsizetype separator = row.indexOf('\n');
            if (separator < 0) continue;
            const QString prefix = QString::fromUtf8(row.mid(separator + 1));
            if (!prefix.isEmpty() && effectiveUrls.first().startsWith(prefix))
                return fail(QStringLiteral("URL rewriting would change the resolved push destination again. Simplify the URL rewrite before publishing."));
        }
    }

    GitPushPreview preview;
    preview.repositoryRoot = root;
    preview.repositoryIdentity = repository.value().commonDirectory;
    preview.localBranch = localBranch;
    preview.sourceCommit = sourceCommit;
    preview.upstream = upstream;
    preview.remote = remote;
    shared::security::RedactionService redactor;
    preview.effectivePushUrl = redactor.redact(effectiveUrls.first());
    preview.pushUrlForExecution = effectiveUrls.first();
    preview.targetBranch = targetBranch;
    preview.establishUpstream = establishUpstream;
    preview.upstreamChange = establishUpstream
        && (configuredUpstreamRemote != remote || configuredUpstreamRef != QStringLiteral("refs/heads/") + targetBranch);
    preview.trackingDescription = establishUpstream
        ? (preview.upstreamChange ? QStringLiteral("Set %1 to track %2/%3 after publication")
            .arg(localBranch, remote, targetBranch) : QStringLiteral("Keep existing tracking for %1").arg(localBranch))
        : QStringLiteral("Do not change branch tracking");
    preview.remoteState = QStringLiteral("notQueried");
    preview.configurationFingerprint = configurationFingerprint(m_runner, root, cancellation);
    if (preview.configurationFingerprint.isEmpty())
        return fail(QStringLiteral("Git configuration could not be fingerprinted safely. Refresh and try again."));

    const QString trackingRef = QStringLiteral("refs/remotes/%1/%2").arg(remote, targetBranch);
    GitCommand trackingCommand{QStringLiteral("cached-push-destination"),
        {QStringLiteral("rev-parse"), QStringLiteral("--verify"), QStringLiteral("--quiet"),
         QStringLiteral("--end-of-options"), trackingRef + QStringLiteral("^{commit}")}, root};
    const auto trackingResult = m_runner.run(trackingCommand, cancellation);
    if (trackingResult.exitCode == 0) {
        preview.remoteTrackingCommit = QString::fromLatin1(trackingResult.standardOutput).trimmed();
        preview.hasCachedRemoteTracking = !preview.remoteTrackingCommit.isEmpty();
        preview.remoteState = QStringLiteral("cached");
    }
    if (preview.hasCachedRemoteTracking) {
        const QString format = QStringLiteral("%H%x00%h%x00%s%x00%an%x00%ae%x00%aI%x1e");
        GitCommand countCommand{QStringLiteral("outgoing-commit-count"),
            {QStringLiteral("rev-list"), QStringLiteral("--count"), sourceCommit,
             QStringLiteral("--not"), trackingRef}, root};
        const auto count = m_runner.run(countCommand, cancellation);
        bool countOk = false;
        const int outgoingCount = QString::fromLatin1(count.standardOutput).trimmed().toInt(&countOk);
        if (count.exitCode == 0 && countOk) {
            preview.outgoingCountAvailable = true;
            preview.outgoingCommitCount = outgoingCount;
        }
        GitCommand logCommand{QStringLiteral("outgoing-commits"),
            {QStringLiteral("log"), QStringLiteral("--date=iso-strict"), QStringLiteral("--format=") + format,
             QStringLiteral("-n20"), sourceCommit, QStringLiteral("--not"), trackingRef}, root};
        const auto log = m_runner.run(logCommand, cancellation);
        if (log.exitCode == 0) {
            for (QByteArray record : log.standardOutput.split(0x1e)) {
                if (record.startsWith("\r\n")) record.remove(0, 2);
                else if (record.startsWith('\n')) record.remove(0, 1);
                if (record.trimmed().isEmpty()) continue;
                const auto fields = record.split('\0'); if (fields.size() < 6) continue;
                GitCommit item;
                item.objectId = QString::fromLatin1(fields[0]);
                item.abbreviatedObjectId = QString::fromLatin1(fields[1]);
                item.subject = QString::fromUtf8(fields[2]).left(200);
                item.author = QString::fromUtf8(fields[3]).left(100);
                item.email = QString::fromUtf8(fields[4]).left(120);
                item.authoredAt = QDateTime::fromString(QString::fromUtf8(fields[5]), Qt::ISODateWithMs);
                if (!item.authoredAt.isValid()) item.authoredAt = QDateTime::fromString(QString::fromUtf8(fields[5]), Qt::ISODate);
                preview.outgoingCommits.append(std::move(item));
            }
            preview.outgoingCommitsTruncated = preview.outgoingCountAvailable
                && preview.outgoingCommitCount > preview.outgoingCommits.size();
        }
    }
    return shared::common::Result<GitPushPreview>::success(std::move(preview));
}

GitPushResult GitService::classifyPushResult(const GitPushPreview& preview,
    const GitCommandResult& processResult) {
    GitPushResult result;
    result.approvedSourceCommit = preview.sourceCommit;
    result.remote = preview.remote;
    result.targetBranch = preview.targetBranch;
    result.diagnostic = boundedDiagnostic(processResult);
    const QString output = QString::fromUtf8(processResult.standardOutput + '\n' + processResult.standardError).toLower();
    QString porcelainRejection;
    bool porcelainUpToDate = false;
    for (const QByteArray& rawLine : processResult.standardOutput.split('\n')) {
        const QByteArray line = rawLine.trimmed();
        if (line.startsWith('=')) porcelainUpToDate |= line.contains("[up to date]");
        if (line.startsWith('!')) porcelainRejection = QString::fromUtf8(line).toLower();
    }
    if (processResult.exitCode == 0) {
        result.remotePublished = true;
        result.trackingConfigured = !preview.upstream.isEmpty()
            && preview.upstream == preview.remote + QLatin1Char('/') + preview.targetBranch;
        result.outcome = porcelainUpToDate || output.contains(QStringLiteral("up to date"))
            || output.contains(QStringLiteral("up-to-date"))
            ? GitPushOutcome::AlreadyUpToDate : GitPushOutcome::Published;
        result.message = result.outcome == GitPushOutcome::AlreadyUpToDate
            ? QStringLiteral("The remote branch already contains the approved commit.")
            : QStringLiteral("The approved commit was published to %1/%2.").arg(result.remote, result.targetBranch);
        return result;
    }
    if (processResult.timedOut) {
        result.outcome = GitPushOutcome::TimedOut;
        result.completionUncertain = processResult.processStarted;
        result.message = processResult.processStarted
            ? QStringLiteral("The push timed out; Git is checking the approved destination to determine whether it completed.")
            : QStringLiteral("Git timed out before contacting the remote.");
    } else if (processResult.cancelled) {
        result.outcome = GitPushOutcome::Cancelled;
        result.completionUncertain = processResult.processStarted;
        result.message = processResult.processStarted
            ? QStringLiteral("The push was interrupted; Git is checking the approved destination to determine whether it completed.")
            : QStringLiteral("Push was cancelled before Git started contacting the remote.");
    } else if (output.contains(QStringLiteral("authentication failed"))
        || output.contains(QStringLiteral("permission denied (publickey"))
        || output.contains(QStringLiteral("could not read username"))
        || output.contains(QStringLiteral("terminal prompts disabled"))
        || output.contains(QStringLiteral("authentication required"))) {
        result.outcome = GitPushOutcome::AuthenticationRequired;
        result.message = QStringLiteral("Git could not authenticate non-interactively. Configure a Git credential helper or an SSH agent, then create a fresh preview and retry.");
    } else if (porcelainRejection.contains(QStringLiteral("hook declined"))
        || output.contains(QStringLiteral("hook declined")) || output.contains(QStringLiteral("pre-push hook"))) {
        result.outcome = GitPushOutcome::HookRejected;
        result.message = QStringLiteral("The repository pre-push hook rejected the update. Review the hook output, make any required changes, and create a fresh preview.");
    } else if (porcelainRejection.contains(QStringLiteral("non-fast-forward"))
        || output.contains(QStringLiteral("non-fast-forward")) || output.contains(QStringLiteral("fetch first"))) {
        result.outcome = GitPushOutcome::NonFastForward;
        result.message = QStringLiteral("The remote branch moved ahead of this preview. Review the remote history and decide how to update your branch; DevHub will not pull or rebase automatically.");
    } else if (porcelainRejection.contains(QStringLiteral("remote rejected"))
        || output.contains(QStringLiteral("remote rejected")) || output.contains(QStringLiteral("[rejected]"))) {
        result.outcome = GitPushOutcome::RemoteRejected;
        result.message = QStringLiteral("The remote rejected this update. Review its policy and the Git diagnostic, then create a fresh preview after resolving the issue.");
    } else {
        result.outcome = GitPushOutcome::Failed;
        result.message = QStringLiteral("Git could not publish this branch. Review the redacted diagnostic and try again after resolving the cause.");
    }
    return result;
}

shared::common::Result<GitPushResult> GitService::executePush(const QString& directory,
    const GitPushPreview& approvedPreview, const tasks::CancellationToken& cancellation,
    std::function<void()> mutationStarted) const {
    std::unique_lock<std::mutex> repositoryLock(mutationLockFor(approvedPreview.repositoryIdentity), std::defer_lock);
    if (!acquireMutationLock(repositoryLock, cancellation)) {
        GitPushResult cancelled; cancelled.outcome = GitPushOutcome::Cancelled;
        cancelled.message = QStringLiteral("Push was cancelled while waiting for the repository mutation slot.");
        cancelled.approvedSourceCommit = approvedPreview.sourceCommit;
        cancelled.remote = approvedPreview.remote; cancelled.targetBranch = approvedPreview.targetBranch;
        return shared::common::Result<GitPushResult>::success(std::move(cancelled));
    }
    const auto current = preparePush(directory, approvedPreview.remote, approvedPreview.targetBranch,
        approvedPreview.establishUpstream, cancellation);
    if (!current) return shared::common::Result<GitPushResult>::failure(current.error());
    if (!samePushApproval(approvedPreview, current.value())) {
        GitPushResult changed;
        changed.outcome = GitPushOutcome::PreviewChanged;
        changed.message = QStringLiteral("The branch, destination, or Git configuration changed after preview. Prepare a fresh push preview and approve it again.");
        changed.remote = approvedPreview.remote;
        changed.targetBranch = approvedPreview.targetBranch;
        changed.approvedSourceCommit = approvedPreview.sourceCommit;
        return shared::common::Result<GitPushResult>::success(std::move(changed));
    }
    // Revalidate the common directory after waiting for the repository-wide
    // mutation slot, so path aliases and linked worktrees share coordination.
    GitRepositoryLocator locator(m_runner);
    const auto identityNow = locator.locate(directory, cancellation);
    if (!identityNow || !identityNow.value().isRepository
        || QDir::cleanPath(identityNow.value().commonDirectory).compare(
            QDir::cleanPath(approvedPreview.repositoryIdentity), Qt::CaseInsensitive) != 0) {
        GitPushResult changed;
        changed.outcome = GitPushOutcome::PreviewChanged;
        changed.message = QStringLiteral("The repository identity changed after preview. Prepare and approve a fresh push.");
        changed.remote = approvedPreview.remote; changed.targetBranch = approvedPreview.targetBranch;
        changed.approvedSourceCommit = approvedPreview.sourceCommit;
        return shared::common::Result<GitPushResult>::success(std::move(changed));
    }
    if (cancellation.isCancellationRequested()) {
        GitPushResult cancelled; cancelled.outcome = GitPushOutcome::Cancelled;
        cancelled.message = QStringLiteral("Push was cancelled before contacting the remote.");
        cancelled.approvedSourceCommit = approvedPreview.sourceCommit;
        cancelled.remote = approvedPreview.remote; cancelled.targetBranch = approvedPreview.targetBranch;
        return shared::common::Result<GitPushResult>::success(std::move(cancelled));
    }

    const QString refspec = approvedPreview.sourceCommit + QStringLiteral(":refs/heads/") + approvedPreview.targetBranch;
    GitCommand command{QStringLiteral("push"),
        {QStringLiteral("push"), QStringLiteral("--porcelain"), QStringLiteral("--no-follow-tags"),
         QStringLiteral("--recurse-submodules=no"), QStringLiteral("--repo=") + current.value().pushUrlForExecution,
         refspec}, approvedPreview.repositoryRoot, std::chrono::milliseconds(300000), true};
    if (mutationStarted) mutationStarted();
    const auto pushed = m_runner.run(command, cancellation);
    GitPushResult result = classifyPushResult(approvedPreview, pushed);
    if ((pushed.timedOut || pushed.cancelled) && !pushed.processStarted) {
        result.outcome = pushed.timedOut ? GitPushOutcome::TimedOut : GitPushOutcome::Cancelled;
        result.message = QStringLiteral("Push was cancelled before Git started contacting the remote.");
        return shared::common::Result<GitPushResult>::success(std::move(result));
    }
    if (pushed.timedOut || pushed.cancelled || (pushed.exitCode < 0 && pushed.processStarted))
        return reconcileInterruptedPush(approvedPreview, pushed);
    if (pushed.exitCode == 0) {
        if (approvedPreview.establishUpstream && approvedPreview.upstreamChange) {
            // Config writes the exact approved tracking pair after publication;
            // branch --set-upstream-to requires a local remote-tracking ref.
            GitCommand localBranchCommand{QStringLiteral("current-local-branch"),
                {QStringLiteral("symbolic-ref"), QStringLiteral("--quiet"), QStringLiteral("--short"), QStringLiteral("HEAD")},
                approvedPreview.repositoryRoot};
            const auto localBranch = m_runner.run(localBranchCommand, cancellation);
            GitCommand localCommitCommand{QStringLiteral("current-local-commit"),
                {QStringLiteral("rev-parse"), QStringLiteral("--verify"), QStringLiteral("--end-of-options"),
                 QStringLiteral("HEAD^{commit}")}, approvedPreview.repositoryRoot};
            const auto localCommit = m_runner.run(localCommitCommand, cancellation);
            const bool sameLocalBranch = localBranch.exitCode == 0
                && QString::fromUtf8(localBranch.standardOutput).trimmed() == approvedPreview.localBranch;
            const bool sameLocalCommit = localCommit.exitCode == 0
                && QString::fromLatin1(localCommit.standardOutput).trimmed() == approvedPreview.sourceCommit;
            const QByteArray currentConfiguration = configurationFingerprint(m_runner, approvedPreview.repositoryRoot, cancellation);
            if (!sameLocalBranch || !sameLocalCommit || currentConfiguration != approvedPreview.configurationFingerprint) {
                result.outcome = GitPushOutcome::PartialTrackingFailure;
                result.message = QStringLiteral("The remote branch was published, but the local branch or Git configuration changed before tracking could be configured. The remote update succeeded; review branch tracking manually.");
                return shared::common::Result<GitPushResult>::success(std::move(result));
            }
            const QString branchConfig = QStringLiteral("branch.%1.").arg(approvedPreview.localBranch);
            GitCommand remoteConfig{QStringLiteral("set-upstream-remote"),
                {QStringLiteral("config"), QStringLiteral("--local"), QStringLiteral("--replace-all"),
                 branchConfig + QStringLiteral("remote"), approvedPreview.remote}, approvedPreview.repositoryRoot,
                std::chrono::milliseconds(10000), true};
            const auto setRemote = m_runner.run(remoteConfig, cancellation);
            GitCommand mergeConfig{QStringLiteral("set-upstream-branch"),
                {QStringLiteral("config"), QStringLiteral("--local"), QStringLiteral("--replace-all"),
                 branchConfig + QStringLiteral("merge"), QStringLiteral("refs/heads/") + approvedPreview.targetBranch},
                approvedPreview.repositoryRoot, std::chrono::milliseconds(10000), true};
            const auto setMerge = setRemote.exitCode == 0 ? m_runner.run(mergeConfig, cancellation) : GitCommandResult{};
            if (setRemote.exitCode != 0 || setMerge.exitCode != 0) {
                result.outcome = GitPushOutcome::PartialTrackingFailure;
                result.message = QStringLiteral("The remote branch was published, but local upstream tracking could not be fully configured. The remote update succeeded; set branch tracking manually.");
                result.diagnostic = boundedDiagnostic(setRemote.exitCode == 0 ? setMerge : setRemote);
                return shared::common::Result<GitPushResult>::success(std::move(result));
            }
            result.trackingConfigured = true;
        }
        qInfo().noquote() << QStringLiteral("Git mutation completed: operation=push durationMs=%1 exitCode=0")
            .arg(pushed.durationMs);
        return shared::common::Result<GitPushResult>::success(std::move(result));
    }
    return shared::common::Result<GitPushResult>::success(std::move(result));
}

shared::common::Result<GitPushResult> GitService::reconcileInterruptedPush(const GitPushPreview& preview,
    const GitCommandResult& interruptedResult) const {
    GitPushResult result;
    result.outcome = interruptedResult.timedOut ? GitPushOutcome::TimedOut : GitPushOutcome::Cancelled;
    result.completionUncertain = true;
    result.approvedSourceCommit = preview.sourceCommit;
    result.remote = preview.remote;
    result.targetBranch = preview.targetBranch;
    result.diagnostic = boundedDiagnostic(interruptedResult);
    GitCommand query{QStringLiteral("reconcile-push"),
        {QStringLiteral("ls-remote"), QStringLiteral("--heads"), QStringLiteral("--"), preview.pushUrlForExecution,
         QStringLiteral("refs/heads/") + preview.targetBranch}, preview.repositoryRoot,
        std::chrono::milliseconds(30000)};
    const auto current = m_runner.run(query, {});
    if (current.exitCode == 0 && !current.timedOut && !current.cancelled) {
        const QByteArray firstLine = current.standardOutput.trimmed().split('\n').value(0).trimmed();
        const QString remoteObject = QString::fromLatin1(firstLine.left(firstLine.indexOf('\t')));
        if (remoteObject == preview.sourceCommit) {
            result.remotePublished = true;
            result.completionUncertain = false;
            const bool trackingAlreadyMatches = !preview.upstream.isEmpty()
                && preview.upstream == preview.remote + QLatin1Char('/') + preview.targetBranch;
            if (preview.establishUpstream && preview.upstreamChange) {
                result.outcome = GitPushOutcome::PartialTrackingFailure;
                result.message = QStringLiteral("The push was interrupted, but Git confirmed that the approved commit reached %1/%2. Local tracking was not configured after interruption; set it manually or prepare a new operation.")
                    .arg(preview.remote, preview.targetBranch);
            } else {
                result.outcome = GitPushOutcome::Published;
                result.trackingConfigured = trackingAlreadyMatches;
                result.message = QStringLiteral("The push was interrupted, but Git confirmed that the approved commit is now at %1/%2.")
                    .arg(preview.remote, preview.targetBranch);
            }
            return shared::common::Result<GitPushResult>::success(std::move(result));
        }
    }
    // Preserve the interruption cause while reporting completionUncertain=true.
    result.completionUncertain = true;
    result.message = QStringLiteral("The push was interrupted. Git could not confirm whether the remote changed; DevHub will not retry automatically. Inspect the destination and create a fresh preview before another push.");
    return shared::common::Result<GitPushResult>::success(std::move(result));
}
shared::common::Result<GitCommandResult> GitService::checkout(const QString& directory, const QString& branch,
    const tasks::CancellationToken& cancellation) const {
    if (!validBranchName(m_runner, directory, branch, cancellation)) return shared::common::Result<GitCommandResult>::failure(
        invalidGitInput(QStringLiteral("Choose a valid branch to check out."), QStringLiteral("checkout")));
    return mutate(directory, QStringLiteral("checkout"), {QStringLiteral("switch"), QStringLiteral("--"), branch}, cancellation);
}
shared::common::Result<GitCommandResult> GitService::createBranch(const QString& directory, const QString& branch,
    const QString& startPoint, const tasks::CancellationToken& cancellation) const {
    if (!validBranchName(m_runner, directory, branch, cancellation)
        || !validRevision(m_runner, directory, startPoint, cancellation))
        return shared::common::Result<GitCommandResult>::failure(
            invalidGitInput(QStringLiteral("Enter a valid branch name and commit start point."), QStringLiteral("create branch")));
    return mutate(directory, QStringLiteral("create-branch"),
        {QStringLiteral("branch"), QStringLiteral("--"), branch, startPoint}, cancellation);
}
shared::common::Result<GitCommandResult> GitService::renameBranch(const QString& directory, const QString& oldName,
    const QString& newName, const tasks::CancellationToken& cancellation) const {
    if (!validBranchName(m_runner, directory, oldName, cancellation)
        || !validBranchName(m_runner, directory, newName, cancellation))
        return shared::common::Result<GitCommandResult>::failure(
            invalidGitInput(QStringLiteral("Enter valid old and new branch names."), QStringLiteral("rename branch")));
    return mutate(directory, QStringLiteral("rename-branch"),
        {QStringLiteral("branch"), QStringLiteral("-m"), QStringLiteral("--"), oldName, newName}, cancellation);
}
shared::common::Result<GitCommandResult> GitService::deleteBranch(const QString& directory, const QString& name,
    const tasks::CancellationToken& cancellation) const {
    if (!validBranchName(m_runner, directory, name, cancellation)) return shared::common::Result<GitCommandResult>::failure(
        invalidGitInput(QStringLiteral("Choose a valid branch to delete."), QStringLiteral("delete branch")));
    auto state = status(directory, cancellation);
    if (!state) return shared::common::Result<GitCommandResult>::failure(state.error());
    if (state.value().branch == name) return shared::common::Result<GitCommandResult>::failure(
        domain::errors::AppError::failure(domain::errors::ErrorCode::InvalidArgument, domain::errors::ErrorCategory::Git,
            QStringLiteral("Check out another branch before deleting this one."), QStringLiteral("Attempt to delete checked-out branch."), QStringLiteral("delete branch")));
    return mutate(directory, QStringLiteral("delete-branch"),
        {QStringLiteral("branch"), QStringLiteral("-d"), QStringLiteral("--"), name}, cancellation);
}
shared::common::Result<GitCommandResult> GitService::stashCreate(const QString& directory, const QString& message,
    const tasks::CancellationToken& cancellation) const {
    QStringList args{QStringLiteral("stash"), QStringLiteral("push")};
    if (!message.trimmed().isEmpty()) args << QStringLiteral("-m") << message.trimmed();
    return mutate(directory, QStringLiteral("stash-create"), std::move(args), cancellation);
}
shared::common::Result<QStringList> GitService::stashList(const QString& directory,
    const tasks::CancellationToken& cancellation) const {
    GitCommand command{QStringLiteral("stash-list"), {QStringLiteral("stash"), QStringLiteral("list"),
        QStringLiteral("--format=%gd%x00%gs%x00%H%x1e")}, directory};
    const auto result = m_runner.run(command, cancellation);
    if (result.exitCode != 0) return shared::common::Result<QStringList>::failure(commandError(result, command.operation));
    QStringList entries;
    for (const QByteArray& row : result.standardOutput.split(0x1e)) {
        if (row.trimmed().isEmpty()) continue;
        const auto fields = row.split('\0');
        if (fields.size() >= 3) entries.append(QStringLiteral("%1\t%2\t%3").arg(
            QString::fromUtf8(fields[0]), QString::fromUtf8(fields[1]), QString::fromLatin1(fields[2])));
    }
    return shared::common::Result<QStringList>::success(std::move(entries));
}
shared::common::Result<GitCommandResult> GitService::stashApply(const QString& directory, const QString& stash,
    bool pop, const tasks::CancellationToken& cancellation) const {
    if (!validStashSelector(stash)) return shared::common::Result<GitCommandResult>::failure(
        domain::errors::AppError::failure(domain::errors::ErrorCode::InvalidArgument, domain::errors::ErrorCategory::Git,
            QStringLiteral("Choose a valid stash entry."), QStringLiteral("Stash selector failed validation."), QStringLiteral("apply stash")));
    return mutate(directory, pop ? QStringLiteral("stash-pop") : QStringLiteral("stash-apply"),
        {QStringLiteral("stash"), pop ? QStringLiteral("pop") : QStringLiteral("apply"), QStringLiteral("--"), stash}, cancellation);
}
shared::common::Result<GitCommandResult> GitService::merge(const QString& directory, const QString& branch,
    const tasks::CancellationToken& cancellation) const {
    if (!validRevision(m_runner, directory, branch, cancellation)) return shared::common::Result<GitCommandResult>::failure(
        invalidGitInput(QStringLiteral("Choose a valid commit or branch to merge."), QStringLiteral("merge")));
    auto state = status(directory, cancellation);
    if (!state) return shared::common::Result<GitCommandResult>::failure(state.error());
    if (!state.value().files.isEmpty()) return shared::common::Result<GitCommandResult>::failure(
        domain::errors::AppError::failure(domain::errors::ErrorCode::InvalidArgument, domain::errors::ErrorCategory::Git,
            QStringLiteral("Commit, stage, or stash local changes before merging."), QStringLiteral("Merge preflight found local changes."), QStringLiteral("merge")));
    return mutate(directory, QStringLiteral("merge"),
        {QStringLiteral("merge"), QStringLiteral("--no-edit"), QStringLiteral("--"), branch}, cancellation);
}
shared::common::Result<GitCommandResult> GitService::abortMerge(const QString& directory,
    const tasks::CancellationToken& cancellation) const {
    GitCommand check{QStringLiteral("merge-state"), {QStringLiteral("rev-parse"), QStringLiteral("--verify"), QStringLiteral("-q"), QStringLiteral("MERGE_HEAD")}, directory};
    if (m_runner.run(check, cancellation).exitCode != 0) return shared::common::Result<GitCommandResult>::failure(
        domain::errors::AppError::failure(domain::errors::ErrorCode::InvalidArgument, domain::errors::ErrorCategory::Git,
            QStringLiteral("There is no merge in progress to abort."), QStringLiteral("MERGE_HEAD is absent."), QStringLiteral("abort merge")));
    return mutate(directory, QStringLiteral("merge-abort"), {QStringLiteral("merge"), QStringLiteral("--abort")}, cancellation);
}
}
