#include "AssistantToolRuntime.h"

#include "AssistantController.h"
#include "ICodexClient.h"
#include "../git/GitCommandRunner.h"
#include "../git/GitExecutableLocator.h"
#include "../git/GitRepositoryLocator.h"
#include "../files/WorkspaceFilePolicy.h"
#include "../notes/NoteSearchService.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QPointer>
#include <QRegularExpression>
#include <QTimer>
#include <algorithm>
#include <cmath>
#include <atomic>
#include <utility>

namespace devhub::application::assistant {
namespace {
using shared::common::Result;

QJsonObject objectSchema(QJsonObject properties = {}, QStringList required = {})
{
    QJsonArray requiredValues;
    for (const auto& key : required) requiredValues.append(key);
    return {{QStringLiteral("type"), QStringLiteral("object")},
        {QStringLiteral("properties"), std::move(properties)},
        {QStringLiteral("required"), requiredValues},
        {QStringLiteral("additionalProperties"), false}};
}

QJsonObject stringSchema(int maximumLength = 256)
{
    return {{QStringLiteral("type"), QStringLiteral("string")},
        {QStringLiteral("minLength"), 1}, {QStringLiteral("maxLength"), maximumLength}};
}

QJsonObject integerSchema(int minimum, int maximum)
{
    return {{QStringLiteral("type"), QStringLiteral("integer")},
        {QStringLiteral("minimum"), minimum}, {QStringLiteral("maximum"), maximum}};
}

ToolDefinition definition(QString wireName, QString description, QJsonObject properties,
    QStringList required = {}, bool workspace = true, qsizetype outputLimit = 48 * 1024)
{
    ToolDefinition result;
    result.name = wireName;
    result.description = std::move(description);
    result.allowedArguments = properties.keys();
    result.requiredArguments = std::move(required);
    result.requiresWorkspace = workspace;
    result.maximumOutputBytes = outputLimit;
    result.validateArguments = [properties = std::move(properties)](const QJsonObject& input, QJsonObject* normalized) {
        for (auto it = input.begin(); it != input.end(); ++it) {
            const auto schema = properties.value(it.key()).toObject();
            const auto type = schema.value(QStringLiteral("type")).toString();
            if ((type == QStringLiteral("string") && !it.value().isString())
                || (type == QStringLiteral("boolean") && !it.value().isBool())
                || (type == QStringLiteral("integer") && (!it.value().isDouble()
                    || std::floor(it.value().toDouble()) != it.value().toDouble()))) return false;
            if (type == QStringLiteral("string")) {
                const auto text = it.value().toString();
                if (text.size() < schema.value(QStringLiteral("minLength")).toInt()
                    || text.size() > schema.value(QStringLiteral("maxLength")).toInt()) return false;
            }
            if (type == QStringLiteral("integer") && (it.value().toInt() < schema.value(QStringLiteral("minimum")).toInt()
                || it.value().toInt() > schema.value(QStringLiteral("maximum")).toInt())) return false;
        }
        *normalized = input;
        return true;
    };
    return result;
}

ToolResult failed(QString code)
{
    ToolResult result;
    result.errorCode = std::move(code);
    result.data.insert(QStringLiteral("error"), result.errorCode);
    return result;
}

QString lifeName(services::ServiceLifecycleState state)
{
    switch (state) {
    case services::ServiceLifecycleState::Stopped: return QStringLiteral("stopped");
    case services::ServiceLifecycleState::Starting: return QStringLiteral("starting");
    case services::ServiceLifecycleState::Running: return QStringLiteral("running");
    case services::ServiceLifecycleState::Stopping: return QStringLiteral("stopping");
    case services::ServiceLifecycleState::Failed: return QStringLiteral("failed");
    }
    return QStringLiteral("unknown");
}

QString healthName(services::ServiceHealthState state)
{
    switch (state) {
    case services::ServiceHealthState::Unknown: return QStringLiteral("unknown");
    case services::ServiceHealthState::Checking: return QStringLiteral("checking");
    case services::ServiceHealthState::Healthy: return QStringLiteral("healthy");
    case services::ServiceHealthState::Unhealthy: return QStringLiteral("unhealthy");
    }
    return QStringLiteral("unknown");
}

QString workspaceStatusName(domain::workspace::WorkspaceStatus status)
{
    switch (status) {
    case domain::workspace::WorkspaceStatus::Ready: return QStringLiteral("ready");
    case domain::workspace::WorkspaceStatus::Missing: return QStringLiteral("missing");
    case domain::workspace::WorkspaceStatus::Inaccessible: return QStringLiteral("inaccessible");
    }
    return QStringLiteral("unknown");
}

bool exactRepositoryRoot(const QString& selectedRoot, const QString& repositoryRoot)
{
    const QString selected = QDir::cleanPath(QFileInfo(selectedRoot).canonicalFilePath());
    const QString actual = QDir::cleanPath(QFileInfo(repositoryRoot).canonicalFilePath());
    return !selected.isEmpty() && !actual.isEmpty()
        && QString::compare(QDir::toNativeSeparators(selected), QDir::toNativeSeparators(actual), Qt::CaseInsensitive) == 0;
}

}

AssistantToolRuntime::AssistantToolRuntime(ICodexClient& client, AssistantController& controller,
    tasks::TaskScheduler& scheduler, workspaces::WorkspaceManager& workspaces,
    workspaces::WorkspaceContext& workspaceContext, services::IServiceDefinitionRepository& serviceDefinitions,
    services::ServiceCoordinator& serviceCoordinator, notes::INotesRepository& notes, QObject* parent)
    : QObject(parent), m_client(client), m_controller(controller), m_scheduler(scheduler), m_workspaces(workspaces),
      m_workspaceContext(workspaceContext), m_serviceDefinitions(serviceDefinitions),
      m_serviceCoordinator(serviceCoordinator), m_notes(notes),
      m_registry([this](const QString& routeId, const ToolResult& result) {
          const auto bytes = QByteArray::fromBase64(routeId.toLatin1(), QByteArray::Base64UrlEncoding);
          const auto parsed = QJsonDocument::fromJson(bytes).array();
          if (parsed.size() != 1) return;
          const QJsonValue id = parsed.at(0);
          const QJsonObject output{{QStringLiteral("success"), result.success},
              {QStringLiteral("contentItems"), QJsonArray{QJsonObject{
                  {QStringLiteral("type"), QStringLiteral("inputText")},
                  {QStringLiteral("text"), QString::fromUtf8(result.compactJson())}}}}};
          (void)m_client.resolveDynamicToolCall(id, output);
      })
{
    registerHandlers();
    const auto specifications = experimentalToolSpecifications();
    m_profileConfigured = m_client.configureDynamicTools(specifications);
    if (m_profileConfigured) {
        const QByteArray identity = QJsonDocument(specifications).toJson(QJsonDocument::Compact);
        m_registryFingerprint = QString::fromLatin1(QCryptographicHash::hash(identity, QCryptographicHash::Sha256).toHex());
    }
    m_registry.bindLifecycle(m_controller, m_client);
    connect(&m_client, &ICodexClient::dynamicToolCallReceived, this, &AssistantToolRuntime::dispatch);
    connect(&m_controller, &AssistantController::accessRevoked, &m_registry, [this] { m_registry.cancelAll(); });
    connect(&m_workspaceContext, &workspaces::WorkspaceContext::workspaceChanged, &m_registry,
        [this] { m_registry.cancelAll(); });
    connect(&m_controller, &AssistantController::toolDisclosureChanged, &m_registry,
        [this] { m_registry.cancelAll(QStringLiteral("disclosure_revoked")); });
    connect(&m_controller, &AssistantController::stateChanged, &m_registry, [this] {
        if (m_controller.operation() == AssistantOperation::Cancelling)
            m_registry.cancelAll(QStringLiteral("cancelled"));
    });
    connect(&m_client, &ICodexClient::turnFinished, &m_registry,
        [this] { m_registry.cancelAll(QStringLiteral("turn_finished")); });
    connect(&m_registry, &AssistantToolRegistry::activityStarted, this,
        [this](const QString&, const QString& wire) { reportActivity(wire, true); });
    connect(&m_registry, &AssistantToolRegistry::activityFinished, this,
        [this](const QString&, const QString& wire, bool success) { reportActivity(wire, false, success); });
}

AssistantToolRuntime::~AssistantToolRuntime()
{
    m_registry.cancelAll(QStringLiteral("runtime_stopped"));
}

QJsonArray AssistantToolRuntime::experimentalToolSpecifications()
{
    const QJsonObject empty = objectSchema();
    const QJsonObject statusArgs = empty;
    const QJsonObject branchArgs = objectSchema({{QStringLiteral("remote"), QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")}}}},
        {QStringLiteral("remote")});
    const QJsonObject logArgs = objectSchema({{QStringLiteral("offset"), integerSchema(0, 1000000)},
        {QStringLiteral("limit"), integerSchema(1, 100)}});
    const QJsonObject noteArgs = objectSchema({{QStringLiteral("noteId"), stringSchema(64)}}, {QStringLiteral("noteId")});
    const QJsonObject searchArgs = objectSchema({{QStringLiteral("query"), stringSchema(128)}}, {QStringLiteral("query")});
    const QJsonObject fileArgs = objectSchema({{QStringLiteral("path"), stringSchema(1024)},
        {QStringLiteral("offset"), integerSchema(0, 1024 * 1024)},
        {QStringLiteral("length"), integerSchema(1, 64 * 1024)}}, {QStringLiteral("path")});
    const QList<QPair<QString, QPair<QString, QJsonObject>>> tools{
        {QStringLiteral("devhub_git_status"), {QStringLiteral("Read Git status in the selected workspace."), statusArgs}},
        {QStringLiteral("devhub_git_branches"), {QStringLiteral("List local or remote Git branches in the selected workspace."), branchArgs}},
        {QStringLiteral("devhub_git_log"), {QStringLiteral("Read a bounded page of Git history in the selected workspace."), logArgs}},
        {QStringLiteral("devhub_workspace_metadata"), {QStringLiteral("Read metadata for the selected registered workspace."), empty}},
        {QStringLiteral("devhub_services_list"), {QStringLiteral("List service names and runtime states for the selected workspace."), empty}},
        {QStringLiteral("devhub_notes_read"), {QStringLiteral("Read a selected-workspace note after note-text disclosure is enabled."), noteArgs}},
        {QStringLiteral("devhub_notes_search"), {QStringLiteral("Search note text in the selected workspace after disclosure is enabled."), searchArgs}},
        {QStringLiteral("devhub_files_read"), {QStringLiteral("Read a bounded file range after file-text disclosure is enabled."), fileArgs}},
        {QStringLiteral("devhub_files_search"), {QStringLiteral("Search workspace file names and relative paths."), searchArgs}}};
    QJsonArray result;
    for (const auto& entry : tools)
        result.append(QJsonObject{{QStringLiteral("type"), QStringLiteral("function")},
            {QStringLiteral("name"), entry.first}, {QStringLiteral("description"), entry.second.first},
            {QStringLiteral("inputSchema"), entry.second.second}});
    return result;
}

QString AssistantToolRuntime::internalName(const QString& wireName)
{
    static const QHash<QString, QString> map{
        {QStringLiteral("devhub_git_status"), QStringLiteral("git.status")},
        {QStringLiteral("devhub_git_branches"), QStringLiteral("git.branches")},
        {QStringLiteral("devhub_git_log"), QStringLiteral("git.log")},
        {QStringLiteral("devhub_workspace_metadata"), QStringLiteral("workspace.metadata")},
        {QStringLiteral("devhub_services_list"), QStringLiteral("services.list")},
        {QStringLiteral("devhub_notes_read"), QStringLiteral("notes.read")},
        {QStringLiteral("devhub_notes_search"), QStringLiteral("notes.search")},
        {QStringLiteral("devhub_files_read"), QStringLiteral("files.read")},
        {QStringLiteral("devhub_files_search"), QStringLiteral("files.search")}};
    return map.value(wireName);
}

void AssistantToolRuntime::registerHandlers()
{
    auto add = [this](ToolDefinition def, AssistantToolRegistry::Handler handler) {
        if (!m_registry.registerTool(std::move(def), std::move(handler)))
            qFatal("Assistant read-tool registration failed.");
    };
    auto gitRead = [this](const ToolCall& call, const ToolCancellation& cancellation,
        std::function<void(ToolResult)> done, QString operation) {
        submitJsonTask(call, cancellation,
            [call, operation = std::move(operation)](const tasks::TaskContext& context) -> Result<QJsonObject> {
                git::GitExecutableLocator locator;
                const auto executable = locator.locate();
                if (!executable) return Result<QJsonObject>::failure(executable.error());
                git::GitCommandRunner runner(executable.value().path);
                git::GitRepositoryLocator repoLocator(runner);
                const auto repo = repoLocator.locate(call.scope.workspaceRoot(), context.cancellationToken);
                if (!repo || !repo.value().isRepository || !exactRepositoryRoot(call.scope.workspaceRoot(), repo.value().root))
                    return Result<QJsonObject>::success({{QStringLiteral("isRepository"), false}});
                git::GitService service(std::move(runner));
                if (operation == QStringLiteral("status")) {
                    const auto value = service.status(call.scope.workspaceRoot(), context.cancellationToken);
                    if (!value) return Result<QJsonObject>::failure(value.error());
                    QJsonArray files;
                    const files::WorkspaceFilePolicy pathPolicy(call.scope.workspaceRoot());
                    for (const auto& entry : value.value().files) {
                        if (!pathPolicy.resolveReadPath(entry.path)
                            || (!entry.originalPath.isEmpty() && !pathPolicy.resolveReadPath(entry.originalPath))) continue;
                        files.append(QJsonObject{{QStringLiteral("path"), entry.path},
                            {QStringLiteral("index"), QString(entry.indexStatus)},
                            {QStringLiteral("workTree"), QString(entry.workTreeStatus)}});
                    }
                    return Result<QJsonObject>::success({{QStringLiteral("isRepository"), true},
                        {QStringLiteral("branch"), value.value().branch}, {QStringLiteral("detached"), value.value().detached},
                        {QStringLiteral("ahead"), value.value().ahead}, {QStringLiteral("behind"), value.value().behind},
                        {QStringLiteral("files"), files}});
                }
                if (operation == QStringLiteral("branches")) {
                    const auto value = service.branches(call.scope.workspaceRoot(), call.arguments.value(QStringLiteral("remote")).toBool(), context.cancellationToken);
                    if (!value) return Result<QJsonObject>::failure(value.error());
                    QJsonArray items;
                    for (const auto& branch : value.value()) items.append(QJsonObject{
                        {QStringLiteral("name"), branch.name}, {QStringLiteral("current"), branch.current},
                        {QStringLiteral("remote"), branch.remote}, {QStringLiteral("upstream"), branch.upstream},
                        {QStringLiteral("ahead"), branch.ahead}, {QStringLiteral("behind"), branch.behind}});
                    return Result<QJsonObject>::success({{QStringLiteral("branches"), items}});
                }
                const int offset = call.arguments.value(QStringLiteral("offset")).toInt(0);
                const int limit = qBound(1, call.arguments.value(QStringLiteral("limit")).toInt(50), 100);
                const auto value = service.history(call.scope.workspaceRoot(), offset, limit, context.cancellationToken);
                if (!value) return Result<QJsonObject>::failure(value.error());
                QJsonArray commits;
                for (const auto& commit : value.value()) commits.append(QJsonObject{
                    {QStringLiteral("id"), commit.objectId}, {QStringLiteral("shortId"), commit.abbreviatedObjectId},
                    {QStringLiteral("subject"), commit.subject}, {QStringLiteral("author"), commit.author},
                    {QStringLiteral("authoredAt"), commit.authoredAt.toString(Qt::ISODateWithMs)}});
                return Result<QJsonObject>::success({{QStringLiteral("commits"), commits},
                    {QStringLiteral("offset"), offset}, {QStringLiteral("limit"), limit}});
            }, std::move(done));
    };

    auto status = definition(QStringLiteral("devhub_git_status"), QStringLiteral("Read Git status in the selected workspace."), {});
    add(std::move(status), [gitRead](const ToolCall& c, const ToolCancellation& x, auto done) mutable { gitRead(c, x, std::move(done), QStringLiteral("status")); });
    auto branches = definition(QStringLiteral("devhub_git_branches"), QStringLiteral("List local or remote branches."),
        {{QStringLiteral("remote"), QJsonObject{{QStringLiteral("type"), QStringLiteral("boolean")}}}}, {QStringLiteral("remote")});
    add(std::move(branches), [gitRead](const ToolCall& c, const ToolCancellation& x, auto done) mutable { gitRead(c, x, std::move(done), QStringLiteral("branches")); });
    auto log = definition(QStringLiteral("devhub_git_log"), QStringLiteral("Read a bounded Git history page."),
        {{QStringLiteral("offset"), integerSchema(0, 1000000)}, {QStringLiteral("limit"), integerSchema(1, 100)}});
    add(std::move(log), [gitRead](const ToolCall& c, const ToolCancellation& x, auto done) mutable { gitRead(c, x, std::move(done), QStringLiteral("log")); });

    add(definition(QStringLiteral("devhub_workspace_metadata"), QStringLiteral("Read selected workspace metadata."), {}),
        [this](const ToolCall& call, const ToolCancellation&, auto done) {
            const auto values = m_workspaces.list();
            if (!values) { done(failed(QStringLiteral("workspace_unavailable"))); return; }
            for (const auto& workspace : values.value()) if (workspace.id().toString() == call.scope.workspaceId()) {
                ToolResult result; result.success = true; result.data = {{QStringLiteral("workspaceId"), workspace.id().toString()},
                    {QStringLiteral("name"), workspace.displayName()}, {QStringLiteral("status"), workspaceStatusName(workspace.status())},
                    {QStringLiteral("favorite"), workspace.isFavorite()}};
                done(std::move(result)); return;
            }
            done(failed(QStringLiteral("workspace_unavailable")));
        });

    add(definition(QStringLiteral("devhub_services_list"), QStringLiteral("List selected workspace service states."), {}),
        [this](const ToolCall& call, const ToolCancellation&, auto done) {
            const auto definitions = m_serviceDefinitions.list(call.scope.workspaceId());
            if (!definitions) { done(failed(QStringLiteral("services_unavailable"))); return; }
            QHash<QString, services::ServiceRuntimeState> runtime;
            for (const auto& value : m_serviceCoordinator.states())
                if (value.definition.workspaceId == call.scope.workspaceId()) runtime.insert(value.definition.id, value);
            QJsonArray list;
            QSet<QString> emitted;
            for (const auto& service : definitions.value()) {
                if (service.workspaceId != call.scope.workspaceId()) continue;
                const auto state = runtime.value(service.id);
                list.append(QJsonObject{{QStringLiteral("id"), service.id}, {QStringLiteral("name"), service.name},
                    {QStringLiteral("state"), lifeName(state.lifecycle)}, {QStringLiteral("health"), healthName(state.health)}});
                emitted.insert(service.id);
            }
            for (auto it = runtime.cbegin(); it != runtime.cend(); ++it) if (!emitted.contains(it.key()))
                list.append(QJsonObject{{QStringLiteral("id"), it.key()}, {QStringLiteral("name"), it->definition.name},
                    {QStringLiteral("state"), lifeName(it->lifecycle)}, {QStringLiteral("health"), healthName(it->health)}});
            ToolResult result; result.success = true; result.data.insert(QStringLiteral("services"), list); done(std::move(result));
        });

    auto noteRead = definition(QStringLiteral("devhub_notes_read"), QStringLiteral("Read bounded note text after explicit disclosure."),
        {{QStringLiteral("noteId"), stringSchema(64)}}, {QStringLiteral("noteId")}, true, 32 * 1024);
    noteRead.requiresNoteReadDisclosure = true;
    add(std::move(noteRead), [this](const ToolCall& call, const ToolCancellation& cancellation, auto done) {
        const QString id = call.arguments.value(QStringLiteral("noteId")).toString();
        if (!call.scope.allowsNoteRead(id)) { done(failed(QStringLiteral("scope_denied"))); return; }
        constexpr qint64 toolNoteBytes = 16 * 1024;
        const auto content = m_notes.readContentBounded(call.scope.workspaceId(), id, toolNoteBytes);
        if (!content || cancellation.isCancellationRequested()) { done(failed(content ? QStringLiteral("cancelled") : QStringLiteral("note_unavailable"))); return; }
        ToolResult result; result.success = true;
        result.data = {{QStringLiteral("noteId"), id}, {QStringLiteral("text"), content.value()}}; done(std::move(result));
    });

    auto noteSearch = definition(QStringLiteral("devhub_notes_search"), QStringLiteral("Search note text after explicit disclosure."),
        {{QStringLiteral("query"), stringSchema(128)}}, {QStringLiteral("query")}, true, 24 * 1024);
    noteSearch.requiresNoteReadDisclosure = true;
    add(std::move(noteSearch), [this](const ToolCall& call, const ToolCancellation& cancellation, auto done) {
        if (!call.scope.notesReadAllowed()) { done(failed(QStringLiteral("scope_denied"))); return; }
        const auto nodes = m_notes.list(call.scope.workspaceId(), false);
        if (!nodes) { done(failed(QStringLiteral("notes_unavailable"))); return; }
        QList<notes::AuthorizedNoteSource> sources;
        for (const auto& node : nodes.value()) if (node.kind == notes::NoteNodeKind::Note && !node.trashed)
            sources.append({node.id, m_notes.contentPath(call.scope.workspaceId(), node.id)});
        submitJsonTask(call, cancellation, [sources = std::move(sources), query = call.arguments.value(QStringLiteral("query")).toString(), cancellation]
            (const tasks::TaskContext& context) -> Result<QJsonObject> {
                Q_UNUSED(context);
                const auto found = notes::NoteSearchService().search(sources, query, cancellation.flag);
                QJsonArray ids; for (const auto& id : found.noteIds) ids.append(id);
                return Result<QJsonObject>::success({{QStringLiteral("noteIds"), ids},
                    {QStringLiteral("filesRead"), found.filesRead}, {QStringLiteral("cancelled"), found.cancelled}});
            }, std::move(done));
    });

    auto fileRead = definition(QStringLiteral("devhub_files_read"), QStringLiteral("Read a bounded workspace file range after explicit disclosure."),
        {{QStringLiteral("path"), stringSchema(1024)}, {QStringLiteral("offset"), integerSchema(0, 1024 * 1024)},
         {QStringLiteral("length"), integerSchema(1, 64 * 1024)}}, {QStringLiteral("path")}, true, 32 * 1024);
    fileRead.requiresFileReadDisclosure = true;
    add(std::move(fileRead), [this](const ToolCall& call, const ToolCancellation& cancellation, auto done) {
        if (!call.scope.filesReadAllowed()) { done(failed(QStringLiteral("scope_denied"))); return; }
        submitJsonTask(call, cancellation, [call](const tasks::TaskContext& context) -> Result<QJsonObject> {
            if (context.cancellationToken.isCancellationRequested()) return Result<QJsonObject>::failure(
                domain::errors::AppError::failure(domain::errors::ErrorCode::OperationCancelled, domain::errors::ErrorCategory::Internal,
                    QStringLiteral("Read cancelled."), {}, QStringLiteral("assistant file read")));
            files::WorkspaceFileService service(files::WorkspaceFilePolicy(call.scope.workspaceRoot()));
            const auto preview = service.readPreview(call.arguments.value(QStringLiteral("path")).toString());
            if (!preview.readable || preview.binary || preview.tooLarge) return Result<QJsonObject>::failure(
                domain::errors::AppError::failure(domain::errors::ErrorCode::PathUnavailable,
                    domain::errors::ErrorCategory::FileSystem, QStringLiteral("The selected file cannot be read as bounded UTF-8 text."),
                    {}, QStringLiteral("assistant file read")));
            const QByteArray utf8 = preview.text.toUtf8();
            const qsizetype offset = call.arguments.value(QStringLiteral("offset")).toInteger(0);
            const qsizetype length = call.arguments.value(QStringLiteral("length")).toInteger(64 * 1024);
            if (offset > utf8.size()) return Result<QJsonObject>::success({{QStringLiteral("text"), QString{}}, {QStringLiteral("truncated"), false}});
            qsizetype begin = offset;
            while (begin < utf8.size() && begin > 0 && (static_cast<unsigned char>(utf8.at(begin)) & 0xC0) == 0x80) ++begin;
            qsizetype end = qMin<qsizetype>(utf8.size(), begin + qMin<qsizetype>(length, 24 * 1024));
            while (end > begin && end < utf8.size() && (static_cast<unsigned char>(utf8.at(end)) & 0xC0) == 0x80) --end;
            const QByteArray bytes = utf8.mid(begin, end - begin);
            const QString text = QString::fromUtf8(bytes.constData(), bytes.size());
            return Result<QJsonObject>::success({{QStringLiteral("text"), text},
                {QStringLiteral("truncated"), end < utf8.size()}});
        }, std::move(done));
    });

    auto fileSearch = definition(QStringLiteral("devhub_files_search"), QStringLiteral("Search safe workspace file names and paths."),
        {{QStringLiteral("query"), stringSchema(128)}}, {QStringLiteral("query")});
    add(std::move(fileSearch), [this](const ToolCall& call, const ToolCancellation& cancellation, auto done) {
        submitJsonTask(call, cancellation, [call, cancellation](const tasks::TaskContext& context) -> Result<QJsonObject> {
            Q_UNUSED(context);
            files::WorkspaceFileService service(files::WorkspaceFilePolicy(call.scope.workspaceRoot()));
            const auto result = service.searchNames(call.arguments.value(QStringLiteral("query")).toString(), cancellation.flag);
            QJsonArray paths; for (const auto& path : result.relativePaths) paths.append(path);
            return Result<QJsonObject>::success({{QStringLiteral("paths"), paths},
                {QStringLiteral("cancelled"), result.cancelled}, {QStringLiteral("budgetExhausted"), result.budgetExhausted}});
        }, std::move(done));
    });
}

void AssistantToolRuntime::submitJsonTask(const ToolCall& call, const ToolCancellation& cancellation,
    std::function<Result<QJsonObject>(const tasks::TaskContext&)> work, std::function<void(ToolResult)> done)
{
    tasks::TaskRequest request;
    request.moduleKey = AssistantToolRegistry::schedulerModuleKey(call);
    const QUuid workspace = QUuid::fromString(call.scope.workspaceId());
    if (!workspace.isNull()) request.workspaceId = workspace;
    request.generation = 1; // unique per-call module key prevents unrelated jobs superseding each other
    auto* poll = new QTimer(this); poll->setInterval(20);
    auto taskHandle = std::make_shared<std::optional<tasks::TaskHandle>>();
    connect(poll, &QTimer::timeout, this, [cancellation, taskHandle] {
        if (cancellation.isCancellationRequested() && *taskHandle) (*taskHandle)->cancel();
    });
    QPointer<QTimer> pollGuard(poll);
    QPointer<AssistantToolRuntime> self(this);
    *taskHandle = m_scheduler.submit<QJsonObject>(request,
        [work = std::move(work), cancellation](const tasks::TaskContext& context) {
            if (cancellation.isCancellationRequested()) return Result<QJsonObject>::failure(
                domain::errors::AppError::failure(domain::errors::ErrorCode::OperationCancelled,
                    domain::errors::ErrorCategory::Internal, QStringLiteral("Read cancelled."), {}, QStringLiteral("assistant tool")));
            return work(context);
        }, [self, pollGuard, cancellation, done = std::move(done)](shared::common::TaskOutcome<QJsonObject> outcome) mutable {
            if (pollGuard) { pollGuard->stop(); pollGuard->deleteLater(); }
            if (!self) return;
            if (std::holds_alternative<shared::common::OperationCancelled>(outcome) || cancellation.isCancellationRequested()) {
                done(failed(QStringLiteral("cancelled"))); return;
            }
            auto value = std::get<Result<QJsonObject>>(std::move(outcome));
            if (!value) { done(failed(QStringLiteral("read_failed"))); return; }
            ToolResult result; result.success = true; result.data = std::move(value).value(); done(std::move(result));
        });
    if (cancellation.isCancellationRequested()) (*taskHandle)->cancel();
    poll->start();
}

ToolScope AssistantToolRuntime::scopeFor(const QJsonObject& parameters, quint64 processGeneration) const
{
    const auto& active = m_workspaceContext.activeWorkspace();
    const auto conversation = m_controller.conversation();
    const auto thread = m_client.threadBinding();
    const bool workspaceMatches = active && !conversation.context.workspaceId.isEmpty()
        && active->accountId().toString() == m_controller.accountId()
        && conversation.context.workspaceId == active->id().toString()
        && exactRepositoryRoot(active->path().canonicalPath(), conversation.context.canonicalRoot);
    const bool authorized = m_controller.accessAllowed() && m_controller.operation() != AssistantOperation::Cancelling
        && m_client.isInitialized() && m_client.profileVerified()
        && m_client.account().mode == CodexAccountMode::ChatGpt && m_client.account().requiresOpenaiAuth
        && processGeneration == m_client.processGeneration() && processGeneration == m_controller.processGeneration()
        && parameters.value(QStringLiteral("threadId")).toString() == thread.threadId
        && parameters.value(QStringLiteral("turnId")).toString() == m_client.activeTurnId()
        && !m_client.activeTurnId().isEmpty() && workspaceMatches;
    const QString workspaceId = workspaceMatches ? active->id().toString() : QString{};
    const QString workspaceRoot = workspaceMatches ? active->path().canonicalPath() : QString{};
    const QString id = QStringLiteral("%1|%2|%3|%4|%5|%6")
        .arg(thread.threadId, parameters.value(QStringLiteral("turnId")).toString(), workspaceId + QLatin1Char('|') + m_controller.accountId())
        .arg(m_controller.accessGeneration()).arg(processGeneration).arg(m_registryFingerprint);
    return ToolScope(id, m_controller.accountId(), workspaceId, workspaceRoot,
        m_controller.accessGeneration(), processGeneration, authorized, {},
        m_controller.notesDisclosureGranted(), m_controller.filesDisclosureGranted());
}

void AssistantToolRuntime::dispatch(const QJsonValue& requestId, const QJsonObject& parameters, quint64 processGeneration)
{
    if (processGeneration != m_client.processGeneration()) return;
    const QString routeId = QString::fromLatin1(QJsonDocument(QJsonArray{requestId})
        .toJson(QJsonDocument::Compact).toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
    ToolCall call;
    call.routeId = routeId;
    call.callId = parameters.value(QStringLiteral("callId")).toString();
    call.name = parameters.value(QStringLiteral("tool")).toString();
    call.arguments = parameters.value(QStringLiteral("arguments")).toObject();
    call.scope = scopeFor(parameters, processGeneration);
    call.expiresAtMonotonicMs = m_registry.monotonicMilliseconds() + 30000;
    (void)m_registry.invoke(std::move(call));
}

void AssistantToolRuntime::reportActivity(const QString& wireName, bool active, bool success)
{
    const QString name = internalName(wireName);
    if (!name.isEmpty()) m_controller.reportToolActivity(name, active, success);
}

} // namespace devhub::application::assistant
