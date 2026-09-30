#include "GitService.h"
#include "GitStatusParser.h"
#include "GitRepositoryLocator.h"
#include "../../shared/security/RedactionService.h"
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QMutex>
#include <QMutexLocker>
#include <memory>
#include <mutex>

namespace devhub::application::git {
namespace {
std::mutex& mutationLockFor(const QString& directory) {
    static QMutex guard;
    static QHash<QString, std::shared_ptr<std::mutex>> lockByPath;
    const QString key = QDir::cleanPath(QFileInfo(directory).absoluteFilePath()).toCaseFolded();
    QMutexLocker lock(&guard);
    auto& repositoryLock = lockByPath[key];
    if (!repositoryLock) repositoryLock = std::make_shared<std::mutex>();
    return *repositoryLock;
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
                pair.first == QStringLiteral("push") ? QStringLiteral("--push") : QStringLiteral("--all"), name}, directory};
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
    if (!commit.isEmpty()) args = {QStringLiteral("show"), QStringLiteral("--format="), QStringLiteral("--no-ext-diff"), commit};
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
    std::unique_lock<std::mutex> repositoryLock(mutationLockFor(directory));
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
    if (remote.trimmed().isEmpty()) return shared::common::Result<GitCommandResult>::failure(
        domain::errors::AppError::failure(domain::errors::ErrorCode::InvalidArgument, domain::errors::ErrorCategory::Git,
            QStringLiteral("Choose a remote to fetch."), QStringLiteral("Remote name is empty."), QStringLiteral("fetch")));
    return mutate(directory, QStringLiteral("fetch"), {QStringLiteral("fetch"), remote}, cancellation);
}
shared::common::Result<GitCommandResult> GitService::pull(const QString& directory, const QString& remote,
    const QString& branch, const QString& explicitStrategy, const tasks::CancellationToken& cancellation) const {
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
    args << remote << branch;
    return mutate(directory, QStringLiteral("pull"), std::move(args), cancellation);
}
shared::common::Result<GitCommandResult> GitService::push(const QString& directory, const QString& remote,
    const QString& branch, bool setUpstream, const tasks::CancellationToken& cancellation) const {
    QStringList args{QStringLiteral("push")}; if (setUpstream) args.append(QStringLiteral("--set-upstream"));
    args << remote << branch; return mutate(directory, QStringLiteral("push"), std::move(args), cancellation);
}
shared::common::Result<GitCommandResult> GitService::checkout(const QString& directory, const QString& branch,
    const tasks::CancellationToken& cancellation) const {
    if (branch.trimmed().isEmpty()) return shared::common::Result<GitCommandResult>::failure(
        domain::errors::AppError::failure(domain::errors::ErrorCode::InvalidArgument, domain::errors::ErrorCategory::Git,
            QStringLiteral("Choose a branch to check out."), QStringLiteral("Branch is empty."), QStringLiteral("checkout")));
    return mutate(directory, QStringLiteral("checkout"), {QStringLiteral("checkout"), branch}, cancellation);
}
shared::common::Result<GitCommandResult> GitService::createBranch(const QString& directory, const QString& branch,
    const QString& startPoint, const tasks::CancellationToken& cancellation) const {
    if (branch.trimmed().isEmpty() || startPoint.trimmed().isEmpty()) return shared::common::Result<GitCommandResult>::failure(
        domain::errors::AppError::failure(domain::errors::ErrorCode::InvalidArgument, domain::errors::ErrorCategory::Git,
            QStringLiteral("Enter a branch name and start point."), QStringLiteral("Branch creation arguments were incomplete."), QStringLiteral("create branch")));
    return mutate(directory, QStringLiteral("create-branch"), {QStringLiteral("branch"), branch, startPoint}, cancellation);
}
shared::common::Result<GitCommandResult> GitService::renameBranch(const QString& directory, const QString& oldName,
    const QString& newName, const tasks::CancellationToken& cancellation) const {
    if (oldName.trimmed().isEmpty() || newName.trimmed().isEmpty()) return shared::common::Result<GitCommandResult>::failure(
        domain::errors::AppError::failure(domain::errors::ErrorCode::InvalidArgument, domain::errors::ErrorCategory::Git,
            QStringLiteral("Enter both branch names."), QStringLiteral("Branch rename arguments were incomplete."), QStringLiteral("rename branch")));
    return mutate(directory, QStringLiteral("rename-branch"), {QStringLiteral("branch"), QStringLiteral("-m"), oldName, newName}, cancellation);
}
shared::common::Result<GitCommandResult> GitService::deleteBranch(const QString& directory, const QString& name,
    const tasks::CancellationToken& cancellation) const {
    auto state = status(directory, cancellation);
    if (!state) return shared::common::Result<GitCommandResult>::failure(state.error());
    if (state.value().branch == name) return shared::common::Result<GitCommandResult>::failure(
        domain::errors::AppError::failure(domain::errors::ErrorCode::InvalidArgument, domain::errors::ErrorCategory::Git,
            QStringLiteral("Check out another branch before deleting this one."), QStringLiteral("Attempt to delete checked-out branch."), QStringLiteral("delete branch")));
    return mutate(directory, QStringLiteral("delete-branch"), {QStringLiteral("branch"), QStringLiteral("-d"), name}, cancellation);
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
    if (stash.trimmed().isEmpty()) return shared::common::Result<GitCommandResult>::failure(
        domain::errors::AppError::failure(domain::errors::ErrorCode::InvalidArgument, domain::errors::ErrorCategory::Git,
            QStringLiteral("Choose a stash entry."), QStringLiteral("Stash selector is empty."), QStringLiteral("apply stash")));
    return mutate(directory, pop ? QStringLiteral("stash-pop") : QStringLiteral("stash-apply"),
        {QStringLiteral("stash"), pop ? QStringLiteral("pop") : QStringLiteral("apply"), stash}, cancellation);
}
shared::common::Result<GitCommandResult> GitService::merge(const QString& directory, const QString& branch,
    const tasks::CancellationToken& cancellation) const {
    auto state = status(directory, cancellation);
    if (!state) return shared::common::Result<GitCommandResult>::failure(state.error());
    if (!state.value().files.isEmpty()) return shared::common::Result<GitCommandResult>::failure(
        domain::errors::AppError::failure(domain::errors::ErrorCode::InvalidArgument, domain::errors::ErrorCategory::Git,
            QStringLiteral("Commit, stage, or stash local changes before merging."), QStringLiteral("Merge preflight found local changes."), QStringLiteral("merge")));
    return mutate(directory, QStringLiteral("merge"), {QStringLiteral("merge"), QStringLiteral("--no-edit"), branch}, cancellation);
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
