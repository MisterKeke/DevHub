# Codex compatibility

This document supersedes the release whitelist and version-bound conversation
rules in the earlier Assistant phase reports. Compatibility is a property of
an executable, an implemented protocol adapter, and a verified restriction
profile. A semantic version is diagnostic information, never admission evidence.
No promise is made about every historical or future Codex release.

## Admission and evidence

`CodexExecutableLocator` resolves a canonical native executable, checks its
bounded `--version` response, hashes its bytes, and delegates schema inspection.
Discovery, hashing, directory monitoring, schema parsing and comparison run on a
dedicated worker thread. The hash is checked before and after acquisition. On
Windows, subsequent launch/inference checks use a process-local measured digest
keyed by volume/file identity, size, creation, write and change times, protected
by a Windows level 2 read-cache oplock. A broken oplock or metadata change requires
fresh measurement, including same-size writes with identical timestamps. The
oplock breaks without application acknowledgement and permits other readers,
writers and deletion; it does not wait for the UI event loop. If a read-cache
oplock cannot be obtained, the digest cannot grant cached admission. UI getters
never reread the executable. A file
watcher revokes the connection on any change notification, including replacement
at the same path. Missing executables, malformed versions, unsupported schema
commands, malformed/truncated schemas, limits and cancellation fail explicitly.

The application-facing `CodexCompatibilityReport` separates identity, protocol
compatibility, runtime policy and feature availability. It includes stable reason
codes, explanations, executable/version/binary identities, selected adapter,
schema/configuration/policy identities and evidence descriptions. Settings show
Compatible, Verification required or Incompatible. Protocol acceptance alone
does not grant native Send, history resume or tool admission.

Schema evidence lives in a bounded, private process-local registry populated only
by fresh acquisition. A caller-created report or an editable cache assertion
cannot admit a native launch. The disk cache contains raw schemas, never a trusted
compatibility or behavior verdict. Every acquisition compares fresh schemas;
matching cache data is diagnostic, and corrupt entries are replaced atomically.
Sixteen digest-named bundles are retained under `CacheLocation/codex-compatibility-v1`.

## Protocol profiles

| Profile | Fixture anchor | Resume | Live views |
| --- | --- | --- | --- |
| `legacy-v1` | 0.160.1 | Complete legacy thread history | Full items |
| `paged-resume-v1` | 0.162.0-alpha.17.2 | Bounded full `initialTurnsPage`, no next cursor | Empty notLoaded starts; one final agent summary |

Fixture versions identify immutable adapter implementation data. They are not a
list of admitted releases. Multiple releases can select the same adapter after
schema comparison. Both profiles require initialize/initialized, an absolute
existing Codex home, public ChatGPT workspace routing (`backendOrigin` and
`chatgptAccountId`), account/read, model/list, effective config and requirements,
the four runtime inventories, strict request/response correlation, native thread
start/resume/read, bounded complete history, dynamic tool specifications/calls,
text item events, interrupt and validated terminal states.

Both opt into `experimentalApi`: explicit legacy history and environment
suppression depend on that surface even when dynamic tools are empty. Every
start/turn suppresses environments and extra roots. Paged resume may reconstruct
one local environment only when its cwd equals DevHub's scratch directory and
its roots are empty. Adapter semantics describe these restrictions independently
of the reported version. There is no independently verified stable fallback yet.

Incoming harmless extra informational fields and unrelated notifications are
tolerated. Identity, correlation, status enums, policy acknowledgements, actionable
items, tool names/arguments and approval contracts retain strict checks. Unknown
server requests receive JSON-RPC method-not-found; they are never approved or
executed. Optional resume/live behavior comes from named profile capabilities.

## Schema acquisition and comparison

The official [OpenAI App Server documentation](https://learn.chatgpt.com/docs/app-server)
was consulted on 2026-10-20. It documents executable-specific
`codex app-server generate-json-schema --out <directory>` and the
`capabilities.experimentalApi` initialize opt-in. It also documents experimental
`historyMode` and the newer paginated history APIs. These facts do not establish
that a particular binary enforces DevHub's restrictions.

The inspector generates in a disposable home/directory with a filtered
environment. Both implemented adapters require experimental restriction fields,
so it directly requests `generate-json-schema --out <directory> --experimental`.
Generating a stable bundle first cannot establish either implemented profile and
unnecessarily spends the acquisition budget twice. The generation option is executable-specific; the current public
page does not enumerate it. Rejection produces `schema.command-unsupported`,
without silently dropping experimental restriction fields.

Acquisition has a 60 second total deadline, 64 KiB output per command
across stdout/stderr, 1,024 JSON files, 4 MiB per file and 32 MiB total. Directory monitoring
also bounds entries and rejects links. Binary hashing is capped at 512 MiB and
five seconds. Schema comparison has a separate 15 second processing bound per
adapter, a work budget, and interruption checks. Proven nonrecursive reference
pairs are memoized within each comparison; recursive assumptions never escape
their enclosing comparison. Windows jobs attach at process creation and kill descendants on
cancellation/deadline/cleanup. The next inspection waits for the previous child
to stop. Acquisition currently requires Windows process-tree containment.

The dedicated schema comparator supports per-file and aggregate layouts,
`definitions`/`$defs`, local file references and JSON pointers. It rejects remote
references, escapes from the bundle, unsupported constraints and excessive
recursion/work. Reference names and union ordering do not need to match. Required
methods and consumed fields are compared rather than the whole generated bundle.
New mandatory request fields, missing identity/history/policy requirements and
changed actionable enums fail. Type/constraint changes are deliberately
conservative. Generated schemas are never installed in the limited value
validator; that validator continues using reviewed adapter snapshots.

`tools/freeze-codex-contract.py` normalizes the corresponding supported layouts
for deterministic offline fixture maintenance. It does not execute Codex.

## Runtime verification

Each launch checks the filtered environment, account, home, cwd, launch overrides,
effective configuration, configuration layers, managed requirements, and feature,
hook, MCP and plugin inventories. Enabled inherited servers trigger the existing
bounded relaunch with explicit per-server disable overrides. Inventory errors,
incomplete pages or active restricted integrations prevent admission.

MCP inventories include disabled configuration entries. A listed entry is
accepted only when the same launch's effective configuration explicitly disables
that server, it exposes no tools/resources/templates or discovery errors, and
any reported runtime connection status is `disabled`. Process-wide responses
may omit that status or return null; those entries still require configuration
corroboration. Unknown, duplicate or malformed entries remain blocked. MCP
validation waits for the effective config regardless of RPC reply order. An
enabled inherited server still triggers the single bounded child-only relaunch
when its inventory arrives first; unrelated restriction failures prevent retry.
No shared configuration files are changed.

The verification worker closes its unused input pipe, and every noninteractive
CLI control receives null stdin explicitly. This prevents `codex exec` from
waiting for desktop-owned input before consuming its synthetic prompt. Hook
controls require a successful CLI exit and a local model round trip; timeouts
produce failed evidence and still close the local server. Failed verification
shows a reviewed check label without exposing raw stderr or protocol payloads.

Settings' **Verify runtime restrictions** explicitly runs the embedded synthetic
capability harness, followed by the current dynamic-tool inventory check. Helpers
are extracted from application resources into a disposable directory and run
with native Python 3 in isolated mode. No executable/script/verdict path can be
supplied through settings. The harness uses disposable homes, local synthetic
files and a loopback scripted model, including positive and negative controls for
shell/native access, hooks, inherited MCP, plugins, environment suppression,
injected tool calls and offered tool inventory. No credentials or real inference
are used. Controls intentionally exercise synthetic local capabilities.

The run has a three minute total deadline, bounded output, cancellation and a
Windows job containing its descendants. The executable identity is checked again
afterward. Failure, interpreter absence or unsupported behavior leaves
Verification required. Successful evidence is reused within a client session
and across application restarts for the same executable path/version/binary,
adapter, schema, effective configuration/environment, policy and tool inventory.
The binding also hashes all embedded verification scripts; unrelated UI/debug
rebuilds do not invalidate it. Changes to C++ verifier semantics must bump the
protected-evidence format revision.

On Windows, a bounded, atomically written record in the dedicated application
cache is encrypted and authenticated with current-user DPAPI and binding-specific
entropy. No plaintext settings flag, imported verdict, machine-wide key or secret
in the repository can grant admission. Only successful native verification can
write a record. Every launch still measures the executable, inspects its schema,
checks the current account/configuration and all runtime inventories, and installs
invalidation watches before consulting that record. Missing, changed, corrupt or
unprotectable records leave verification required. Unsupported platforms have no
plaintext fallback. A manual recheck revokes the saved record before starting;
verification failures and observed configuration/executable changes revoke it too.
Storage failures retain only session evidence and are explained in the result.

Selection changes, reconnects with changed identities and failed restriction checks revoke
stale admission. Editing the draft executable cancels active verification.
Effective configuration identity also includes a digest of the filtered process
environment. Metadata watches on reported configuration layers and home/project
configuration paths revoke admission on changes; configuration contents and
credentials are not read by DevHub.

Historical fixtures do not record a binary digest and explicitly exclude release
approval. Their cases inform this verifier; their version-labelled booleans cannot
authorize an installed binary. Consequently even a known fixture release initially
requires fresh behavior verification. Account/home/workspace checks still apply
after a behavior check passes. Existing developer live-validation opt-ins remain
separate and cannot be enabled in the production library through settings.

## Conversation migration

The `semantic-v2:` policy fingerprint combines effective launch restrictions,
adapter binding semantics and the complete DevHub tool inventory. Account and
hashed Codex home remain separate binding identities; the native scratch cwd and
DevHub workspace ID/root are checked independently. Compatible upgrades can keep
the same semantic association. Version and binary identity remain in diagnostic
and evidence records and do not force a new conversation by themselves.

Known legacy fingerprints are reconstructed only for the two original formats,
with exactly the current restrictions and tool inventory. A legacy association
may attempt resume only after current protocol/runtime/account/home/workspace
verification. Full correlated history and policy acknowledgements must pass
before the controller saves its replacement semantic fingerprint. Unknown legacy
hashes, changed accounts/homes/workspaces, changed tools or weaker policies
require a new chat; no metadata is rewritten merely because discovery succeeds.
The existing SQL compatibility_profile column stores the tagged semantic value;
there is no destructive database rewrite or transcript migration.

## Integrity, coverage and limits

Tool-inventory evidence now uses `sha256-canonical-json-v1`, shared by producer
and consumer. It ignores checkout line endings/JSON formatting while retaining
array order and every semantic inventory/schema value. The historical Phase 9
inventory digest was converted from its existing source inventory, without
regenerating runtime evidence or relaxing any behavioral assertion.

Source regression coverage includes both profiles, a compatible new release,
harmless additions, required/unsupported schema changes, file/$defs layouts,
untrusted reports and corrupt caches, executable replacement, cancellation,
inventory failures, semantic binding and legacy migration. Compatibility is
intentionally conservative about unimplemented constraints, new actionable
variants, history mechanisms and runtime behavior. Behavior verification needs
Windows job support and native Python 3. Synthetic verification is not a model
entitlement, authenticated inference or packaged-release certification.

For the initial implementation task, only source/document inspection, source edits,
inventory-digest conversion and Git status/diff inspection were performed. No
tests, builds, CMake configuration/generation, applications, servers, native
probes, Codex discovery/schema generation, authenticated inference, packaging or
deployment were run in that initial task.

The follow-up freeze/connection review moved inspection off the UI thread,
removed executable-byte reads from admission getters, requested the experimental
schema directly, and bounded/memoized comparison. Its offline validation uses the
fake executable plus an optional existing generated bundle supplied through
`DEVHUB_TEST_CODEX_SCHEMA_DIRECTORY`; it never generates schemas by executing an
installed Codex. The existing 447-file experimental bundle selected
`paged-resume-v1` in approximately 160 ms in a Debug comparator build. Regression
cases include continued UI events during inspection, replacement after cancel,
experimental command selection, stale generation suppression, changed binary
metadata, and native rejection before launch when evidence is absent.

First follow-up validation: the Debug `devhub` application and all five targeted
Assistant test targets built successfully. CTest passed compatibility, protocol,
process, client and controller suites (308 passed checks; two native opt-in checks
skipped). No installed Codex discovery, fresh native schema generation, sign-in,
authenticated inference, packaging or deployment was performed in that first review.

The subsequent runtime-inventory review reproduced 0.163.0-alpha.10.2 reporting
`unified_exec=true` with stage `stable` even though effective config retained the
requested false override. The current [configuration reference](https://learn.chatgpt.com/docs/config-file/config-reference)
documents this backend as always enabled with user opt-outs ignored. Its stable
selector is accepted only when the independent `shell_tool` inventory flag is
explicitly false. All required inventory entries and configuration restrictions
remain mandatory; this exception never grants behavior verification or Send.
Enabled native features now carry their allowlisted names in rejection reasons,
and the UI explains the specific restriction instead of blaming inherited config
for every inventory mismatch. Executable-only recheck results are labelled
separately from connection/behavior status.

Readiness getters perform no filesystem I/O. Watchers revoke stale state, while
actual native thread/history/turn requests, dynamic-tool dispatch and results
recheck executable/configuration metadata before acting. The settings model
picker rebuilds only when its catalog changes. On Windows, behavior verification
prefers native `python.exe` over a `python3.exe` Store alias.

This review used disposable homes with the installed binary: native profile
checks passed, all 27 offline capability cases passed, and dynamic-tool checks
passed against the reviewed inventory. No credentials, sign-in or real inference
were used. A native connection timing check measured a 36 ms launch call and a
41 ms maximum event-loop gap outside Qt Creator. These measurements do not include
debugger overhead. The eight targeted Assistant/settings/tool suites passed 500
checks, with two unrelated native opt-in tests skipped. External evidence files
are diagnostic only; the application still acquires its own session evidence via
**Verify runtime restrictions**.
