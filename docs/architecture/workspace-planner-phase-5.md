# Workspace Planner Phase 5 integration

## Supported integrations

- Task details can open the task's local Git branch in the existing Git module, open saved GitHub Issue and Pull Request URLs, open the registered repository folder, and open the workspace Notes module.
- A task can link to one service definition already saved in the active workspace. The link stores the service definition ID; imported titles and descriptions are never interpreted as commands.
- Starting that service is an explicit task-detail action. DevHub rechecks the signed-in session, active workspace, workspace removal state, saved service ownership, task-to-service link, and scheduler shutdown state before passing the saved definition and canonical workspace path to `ServiceCoordinator`.
- Start requests are asynchronous. A pending start can be cancelled; switching workspace, locking the account, removing the workspace, or shutting down cancels a pending Planner start. A service already running after a successful start remains managed by the existing Services module.
- The Planner stores at most 20 finished action records per task, plus any in-progress record. A record contains timestamps, result, duration, a bounded error summary, and a reference to the existing service stdout source. Output stays in the existing bounded Logs buffers.
- Failed starts post a deduplicated Service notification. A successful start is reported in the status bar and task history; the user can open the linked service or its Logs view from task details.
- Ctrl+K includes create, find, overdue, in-progress, expand/collapse, and open-task actions. Task search results carry an opaque task ID payload; the normal workspace-scoped Planner controller resolves it before opening details.

## Unsupported workflows

The first supported task action is `service.start:<saved-service-id>`. Planner does not yet launch arbitrary builds, tests, terminals, or Git commands. It does not mutate Git state, infer commands from task content, or trigger actions during import, task opening, status changes, workspace selection, or Overview initialization. The general workflow examples in the Phase 5 brief require a separately designed, allow-listed action model and are outside this integration.

Task notes remain available in Planner. The contextual Notes button opens workspace Notes because there is no task-to-Notes relationship in the current data model. Related logs are available when the linked service has a managed-service log source.

## Assistant boundary

DevHub-owned Planner tasks are not exposed to the Assistant in this phase. The existing SomethingQt task reader is a separate, explicitly controlled read-only source and does not provide the permission model for this database. A Planner source requires explicit task-data disclosure consent, account and workspace authorization, conversation-scope binding, cancellation when those scopes change, bounded result shapes, and treatment of task descriptions as untrusted content. Until those prerequisites are part of the Assistant's source contract, Planner data remains outside Assistant context. No task mutation or command execution tool is added.
