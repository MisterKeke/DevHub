# ADR 0006: Assistant through Codex App Server in the existing hybrid UI

- Status: Accepted
- Date: 2026-10-09

## Context

The Assistant implementation plan replaces Quick Ask at module index 9. Current
code already differs from two historical decisions: MainWindow hosts MainShell.qml
through QQuickView and QWidget::createWindowContainer, and external SQLite queries
use NativeSqliteQueryExecutor. Feature pages remain Qt Widgets in a DevHub-owned
nonmodal QDialog. Internal persistence still uses Qt SQL/QSQLITE.

The Phase 0 candidate is codex-cli 0.160.1. Its generated protocol schemas and
isolated runtime inventory establish a wire contract, but the tested launch
profile fails the text-only capability gate. No inference was performed.

## Decision

Preserve the existing QML shell and Widgets module-dialog integration. The later
AssistantPage replaces Quick Ask at index 9 and migrates nav.quickAsk to
nav.assistant. Do not add a parallel chat entry or migrate other modules for this
feature. This supersedes ADR 0001's rejection of QML for the shell; its Qt baseline,
Widgets feature-page approach and exclusion of WebEngine remain applicable.

Use a dedicated installed native Codex App Server child with structured stdio,
managed by DevHub. Reuse Codex's supported authentication, without copying or
reading credentials, implementing token renewal, introducing an API key, or
falling back to a separate inference backend. Keep transport, protocol, controller,
presentation and metadata persistence separate as specified in the implementation
plan. Defer application tools and any external MCP adapter to their later phases.

Enable inference only for an explicitly tested version and a profile that enforces
the required capability scope. Successful initialization, configuration echoes,
read-only sandbox settings and empty environment selection are insufficient.
The current 0.160.1 candidate profile is rejected; offline transport development
can proceed against its fixtures while capability enforcement remains a gate.

Recognize the existing native external SQLite executor as the concrete exception
to ADR 0003's initial Qt-only backend decision. Preserve its cancellation and query
boundaries, and preserve Qt SQL for internal storage. This record does not introduce
another database implementation.

## Consequences

Assistant fits the current application without a browser runtime, Node/Python
production dependency or inference SDK. The user may need supported external
authorization once; chat itself remains in DevHub. Historical ADRs remain immutable.

Version drift and inherited capabilities require explicit revalidation. Phase 0
fixtures, rejected-profile evidence and outstanding release gates are documented
in [assistant.md](../architecture/assistant.md). No production Assistant UI or
backend is added by this decision.

## Alternatives considered

- Retaining Quick Ask's browser Send flow does not provide in-application chat.
- A direct API client changes authentication and billing requirements.
- Terminal scraping does not provide a dependable event/approval contract.
- Rewriting all modules in QML expands the feature beyond its required scope.
- An MCP bridge adds unnecessary lifecycle and authorization work to text-chat MVP.
