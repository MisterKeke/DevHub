# Assistant Phase 9 Report

Date: 2026-10-11

## Scope

Phase 9 adds five application tools: `devhub_notes_edit`, `devhub_services_start`,
`devhub_services_stop`, `devhub_services_restart`, and `devhub_project_open`.
They join the nine Phase 8 reads. Every mutation requires a new Allow once
decision. Native command/file approvals remain declined. Delete/reset, arbitrary
shell/SQL, service definition editing, file writes, persistent grants and MCP
are absent from the inventory.

The implementation targets the user's installed `0.162.0-alpha.17.2`; the same
contracts have offline regression coverage for the pinned 0.160.1 profile.
Authenticated inference and native lifecycle evidence remain pending, following
the user's earlier instruction to finish offline checks. The user subsequently
requested usable production sending. Production chat is now enabled for exactly
`0.162.0-alpha.17.2`, after per-launch account, model, profile and scope checks.
This product admission decision does not claim that authenticated model or
clean-machine release validation has been completed. Native chat for 0.160.1
remains gated; its in-memory regression coverage and opt-in live harness remain.

## Approval and execution ownership

`AssistantApprovalCoordinator` holds normalized arguments, immutable scope,
deadline, cancellation token, effect and result callback on the owner thread.
The page shows plain-text inline cards and complete proposed note text with
Allow once and Deny. Decisions are consumed before execution. Hiding the page
denies pending cards. Expiry, Stop, lock/sign-out, workspace/access changes,
terminal turns, child loss and shutdown revoke calls. There are no persistent
or session mutation grants. Stop remains available during approval and service
completion waits.

Notes edits require a lowercase SHA-256 expected hash and at most 32 KiB UTF-8
content. The repository must still identify a live note in the captured
workspace. NotesPage rejects dirty editors and recovery journals at admission
and immediately before saving, then uses existing `saveContent` hash checks.
Successful edits refresh a clean open editor and its hash. The shared Notes
repository now refuses link components before writes. Both UI and Assistant
use that policy. A failure after entering the write path reports an uncertain
outcome and cannot trigger automatic retry.

Service tools use saved definitions and ServiceCoordinator's existing validator,
repository and launcher. Approval binds every definition field and process
generation/PID/lifecycle. Admission is not completion: the tool waits for
Running, Stopped or Failed. Restart waits for a replacement with its originating
action identity, not merely any process with the same service ID. Scope,
deadline and definition are checked again before a delayed replacement launches.
Cancelling an issued action reports an uncertain outcome and revokes its pending
replacement/restart scheduling; it does not claim to undo an already launched
process. Cancellation is bound to that action and cannot revoke a newer UI
action. Responses exclude commands, environment values and raw process errors.
The approval names the saved restart policy; cancelled/failed startup cannot
create delayed retries.

Workspace opening uses MainWindow's existing activation workflow and the same
Notes flush/recovery and Database pending-change prompts as workspace selection.
It rechecks scope and target path after prompts, and calls WorkspaceManager
activation once. The result is resolved after activation succeeds and before
workspaceChanged revokes the old chat scope. Missing editor/workflow integration
fails closed.

Mutation terminal outcomes survive read-cache eviction for the runtime lifetime.
A bounded mutation budget rejects new calls rather than losing old identities.
Late completion cannot overwrite cancellation. Restored tool history contains
identity only and never dispatches mutations. Reconnect does not replay a lost
action. Uncertain actions show a safe notice to inspect the service/note before
requesting them again.

## Checklist evidence

| Phase 9 point | Implementation and regression evidence |
|---|---|
| Coordinator/cards, AllowOnce/Deny/expiry | Five tools × seven decision/lifecycle transitions × two versions (70 cases); UI checks one-use Allow, Deny, plain-text preview and denial on hide. |
| Post-approval scope/entity/hash/runtime validation | Dirty/hash/external-note conflicts, changed service definition/process, removed target, denied transition, and revalidation at delayed replacement launch. |
| Existing-definition service actions and actual completion | Production ServiceCoordinator with controlled delayed process signals; no success at admission, old exit before replacement startup, delayed failure and independent action identity. |
| Hash edits coordinated with editor/recovery | Real NotesPage preserves dirty text and journals; shared NotesRepository checks stale hashes, traversal, cross-workspace/account ownership and a Windows storage junction. |
| Guarded project.open through shared transition workflow | Shared MainWindow activation with post-prompt validation; runtime counts one WorkspaceManager activation and zero after denial/invalidation. Existing Notes/Database transition regressions cover their owners. |
| Capability counts and shared business logic | Existing 18 read-count rows plus mutation cases count saves, launches, stops and recordOpened. In-flight and terminal aliases execute once. Shared owners perform actual actions. |
| Cross-phase security/lifecycle and no replay | Phase 7 traversal/ADS/secrets/links; 42 integrated Phase 8 read cases; 70 mutation lifecycle rows plus conflict/restart cases. Cancelled mutation outcomes survive 600 read calls. Junction tests also run outside the restricted runner. |

## Three Phase 8 follow-ups

1. **Tagged experimental specs:** installed alpha accepted all 14 specs exported
   directly from the compiled inventory. A loopback scripted model completed one
   Git read and consumed its response. The offered surface was the 14 application
   tools plus request_user_input. Sanitized evidence and exact inventory are
   checked in with the alpha fixtures. This is offline native compatibility;
   authenticated, realInference and releaseEligible are all false.
2. **Phase 7–9 counts/shared owners:** the read checks now run alongside approved
   once-only Notes, service and workspace mutations. Shared owner tests cover
   Notes/Files extraction behavior and SQL note hash/ownership persistence.
3. **Integrated read lifecycle:** 42 offline client/controller/registry/scheduler
   cases remain covered. The opt-in native harness now includes disclosure
   denial, Stop, lock and actual child termination while a read worker is held
   until revocation. It is compiled but has not run with an authenticated model.
   This remains a release gate, not a passing result.

## Verification and remaining gates

The application and opt-in live harness rebuilt successfully on Qt 6.11 MinGW
13/CMake 3.30.5 on Windows 11. This is not the clean-machine Qt 6.8.3/MSVC
baseline. Final verification:

- Debug CTest: **14/14 suites passed** (protocol, process, client, controller,
  UI, conversation/Notes repository, settings, tools, tool runtime, Files UI,
  Notes UI, feature regressions, reliability and QML runtime), 35.68 seconds.
- After tightening the required admission/revalidation repository counts, Debug
  tool runtime passed again: **151 passed, 0 failed, 0 skipped**. Its 70 mutation
  rows include both wire versions, in-flight/completed aliases, once-only effects
  and exact counts for the intentional validation reads.
- Release CTest: **4/4 suites passed** (tool runtime, registry/tools, Assistant
  UI and Notes UI), 17.14 seconds. Runtime: **151 passed, 0 failed, 0 skipped**;
  approval/editor UI checks pass with assertions disabled too.
- `python tests/codex_contract_test.py`: **13 passed**, including the exported
  14-tool inventory, its exact SHA-256 and sanitized alpha native evidence.
- Real installed-alpha `nativeAlphaOfflineProfile`: **3 passed, 0 skipped** in
  an empty temporary home, through production CodexProcess/CodexClient. It
  confirms profile admission and refusal of unauthenticated thread creation.
- Offline native full-inventory round trip: **all six checks passed**, with
  authentication/inference/release flags false. Evidence:
  `tests/fixtures/codex/0.162.0-alpha.17.2/phase9-offline-tool-verification.json`.
- The default restricted runner skips junction creation. Separate unrestricted
  runs of `workspaceLinksCannotExpandSearchOrReadScope` and the new real-repository
  `noteWritesRejectStorageJunctions` each passed **3/3, 0 skipped**. The note test
  preserves a planted outside file. Default native profile checks are also
  opt-in skips; their separate successful result is recorded above.
- `git diff --check` passed. The authenticated harness is compiled, excluded from
  CTest, and **not executed**.

Reproduce the main runs using the existing `windows-dev` build:

```powershell
& 'D:\Qt\Tools\Ninja\ninja.exe' -C out/build/windows-dev devhub devhub_assistant_tool_runtime_tests devhub_assistant_ui_tests devhub_assistant_tools_tests devhub_notes_search_ui_tests devhub_assistant_live_tests
$testTemp = (Resolve-Path 'out/build/windows-dev/tests/Debug').Path
$env:TEMP = $testTemp; $env:TMP = $testTemp
& 'D:\Qt\Tools\CMake_64\bin\ctest.exe' --test-dir out/build/windows-dev -C Debug -R 'assistant-protocol|assistant-process|assistant-client|assistant-controller|assistant-ui|assistant-conversation-repository|assistant-settings|assistant-tools|assistant-tool-runtime|file-browser-ui|notes-search-ui|feature-regressions|reliability|qml-runtime' --output-on-failure
& 'D:\Qt\Tools\Ninja\ninja.exe' -C out/build/windows-dev -f build-Release.ninja devhub_assistant_tool_runtime_tests devhub_assistant_tools_tests devhub_assistant_ui_tests devhub_notes_search_ui_tests
& 'D:\Qt\Tools\CMake_64\bin\ctest.exe' --test-dir out/build/windows-dev -C Release -R '^devhub.(assistant-tool-runtime|assistant-tools|assistant-ui|notes-search-ui)$' --output-on-failure
python tests/codex_contract_test.py
```

The native live harness stays outside CTest and requires the real-inference
acknowledgement, exact executable, separately authenticated disposable CODEX_HOME
and sentinel documented in assistant-live-integration.md. No default credentials
are copied and no authenticated inference or login/logout occurs in offline
checks. Authenticated capability/lifecycle and clean-machine package acceptance
remain open. Offline coverage is not authenticated-inference evidence.

## Profile warning and module window sizing

The profile rejection warning is separate from version admission. The production
launch overrides restricted features and policies, suppresses inherited notify,
and disables desktop ambient suggestions for its child only. Known desktop
appearance/queue preferences are permitted; unknown fields and attached browser
or computer runtimes still fail the checks. If inherited MCP is the only remaining
conflict, supported `config/read` results supply server names for explicit quoted
disable overrides. The controller performs at most one automatic restart per
manual connection attempt and verifies the full effective profile again before
allowing any thread or turn. Reconnect refreshes the discovered inventory. Global
configuration and credentials are neither copied nor edited.

The controller now preserves safe explanations from `profileChecked` for the
status banner and support diagnostics. It never displays command contents,
server names, configuration values, or unknown raw reason strings. Reconnecting
clears explanations from the previous attempt. Global Codex configuration is
not modified.

The module host now computes size hints from its current page. Previously,
`QStackedWidget` included hidden pages' minimum sizes and could force the
Assistant window beyond the monitor. Opening a module also clamps the decorated
window to the current screen's available area. Approval previews have their own
bounded scrolling area so they cannot displace the composer.

Regression coverage checks a hidden module with an 1800-pixel minimum height,
the blocked-profile banner at 720/850/1120 widths in a 520-pixel-high window,
two pending note previews, visible composer/action controls, and sanitized
profile diagnostics. These are offline UI/controller checks; authenticated
runtime validation remains pending.

## Production sending follow-up

The installed 0.162.0-alpha.17.2 was checked through the production locator,
transport, client and controller using its existing runtime-managed account.
The first effective profile requested inherited MCP isolation. The automatic
restart passed the complete profile check, ChatGPT account/routing and text-model
catalog checks; `AssistantController::canSend()` returned true. No thread, turn,
login/logout or configuration-write request was issued. Sanitized evidence is
`tests/fixtures/codex/0.162.0-alpha.17.2/production-sending-readiness.json`.

The native control also exposed a CLI compatibility defect: a quoted segment
in a dotted override path does not work in this alpha. A single `mcp_servers`
TOML table override with quoted entry names correctly preserves inherited
definitions while disabling their enabled flags. Native disposable-profile
regressions cover dots, quotes and Unicode names, notify suppression and desktop
preferences. Unknown fields, managed conflicts and additional capabilities
remain rejected. This readiness result does not verify an authenticated model
response, cancellation during real inference, or clean-machine packaging.

## Startup and reconnect diagnostics follow-up

An initialize JSON-RPC rejection previously removed its pending request without
closing the connection, leaving the page in Starting indefinitely. That path
now closes the backend. The controller also enforces a 20-second startup deadline
and keeps failures visible until an explicit retry. Reconnect invalidates the
old probe before cancellation and resets the startup deadline.

Debug process, client, controller, UI and tool-runtime suites pass; controller
coverage includes initialize rejection, startup timeout/recovery and reconnect
with a late response from the previous process. The installed alpha was also
checked with the actual AssistantPage under GDB: three reconnects during Starting
and three after readiness passed, with the unsent draft retained and normal exit.
Only account, profile and model-catalog requests were made. No prompt, login or
shared configuration write was performed. These standalone checks do not cover
the full application's bootstrap object.

Qt Creator's local `.qtc_clangd/compile_commands.json` was empty while CMake's
compiler database contained the correct source/Qt include paths. Restoring the
generated database alone was temporary: Qt Creator replaced it with an empty
file on reopening. Its selected preset kit had empty C/C++ compiler assignments
and no Qt version. The local kit now references the installed Qt 6.11.0 and its
MinGW 13.1 compilers, with a corrected PATH environment entry. The project's
initial vcpkg triplet also matches the preset's `x64-mingw-static`. Qt Creator's
bundled clangd checks `CodexProcess.cpp` with CMake's database with zero errors;
reopening Qt Creator regenerates its own database using the repaired kit. No
unsupported external-database preference is left in the Qt Creator 19 project.

The supplied SIGSEGV stack shows the startup timeout calling `CodexClient::stop`,
which unexpectedly enters `CodexProcess::launch` with invalid arguments. The
application's `ApplicationBootstrapper.cpp` object predated the controller header
change and had zero recorded header dependencies. It constructs these Assistant
objects on the stack, so linking it against updated class layouts can corrupt
adjacent objects. This is consistent with the unexpected virtual dispatch; the
executable's static virtual table and the rebuilt standalone tests were correct.
A clean Debug rebuild now records 682 bootstrap dependencies, including the
controller, client and transport headers. Verification of the same startup in
the user's full authenticated application remains necessary to confirm that this
was the sole cause of the reported crash.

## Legacy history scope follow-up

The reconnect failure was traced through a read-only native `thread/read` of the
conversation already associated with DevHub's metadata. Its directory matched
the isolated cache, its provider was OpenAI, and its interrupted turn used legacy
history. The runtime returned `environments: null`, which the pinned schemas
permit for unassigned persisted metadata. The decoder required an array even for
restore/read, causing the active-scope rejection. Sanitized evidence is
`tests/fixtures/codex/0.162.0-alpha.17.2/legacy-thread-history-scope.json`.

Legacy resume/read now accept absent or null environment assignments, while new
threads still require an explicit empty array acknowledgement. Foreign directories,
providers and thread/account associations remain rejected. Every new turn still
sends `environments: []`. Directory
checks require an existing absolute canonical directory and account for Windows
case differences. Protocol failures identify the RPC method and stay visible in
the chat until reconnect, without exposing remote messages or paths.

Regressions cover both pinned versions, legacy history restoration, assigned
environment denial, directory aliases/mismatches, and visible failure followed
by reconnect recovery. Native diagnostics sent no prompt and did not request
login. That initial diagnostic did not run authenticated native resume or model
inference; the subsequent resume check below exposed additional incompatibilities.

## Native resume and New Chat follow-up

The installed alpha's actual `thread/resume` returns paginated history and
reconstructs one environment in DevHub's canonical scratch directory with no
runtime roots. `thread/read` had returned legacy history with null assignments,
so the read-only diagnostic alone did not exercise the failing restore path.

Alpha resume now explicitly requests an ascending `initialTurnsPage` with full
items and a limit one above the 100-turn cap. Hydration uses that page only when
it is complete, bounded, terminal and text-compatible. Pagination continuation,
partial item views, duplicate turns, unsafe items, foreign environments, added
roots, providers or writable/network-enabled sandbox responses remain rejected.
The single scratch-only reconstructed environment is compatible with this resume
response; every admitted turn still sends empty environments and read-only policy.

New Chat after a failed/disconnected restore now reconnects. Its explicit selection
suppresses automatic restoration for the current account/session, including profile
isolation relaunches and later manual reconnects, so the same saved association
cannot trap the user in another restore failure. Existing metadata is retained.

Prompt-free native verification used the production locator, transport, client
and controller against the existing DevHub-associated conversation. The probe
loaded only its local association metadata into a memory repository, leaving the
real DevHub database untouched. Restore passed with `canSend=true`; selecting New
Chat and reconnecting also passed with the empty new conversation still selected,
exactly one restoration and `canSend=true`. Each launch verified the effective
profile after disabling inherited MCP servers in the child. Sanitized evidence is
`tests/fixtures/codex/0.162.0-alpha.17.2/native-resume-sending-readiness.json`.

All five Debug process/client/controller/UI/tool-runtime suites pass. Added
regressions cover native-shaped full-page restoration, scope/partial-history
denial and New Chat recovery following a rejected resume. No prompt, inference,
login/logout or shared configuration write was performed; real-inference live
validation remains pending under its separate opt-in.

## Live turn envelopes and account refresh follow-up

The next send attempt exposed two further native compatibility assumptions.
The alpha returns `itemsView: "notLoaded"` and an empty items array in
`turn/start`, `turn/started` and failed `turn/completed`; text arrives through item events.
A separate empty disposable home was verified signed out through `account/read`
before a synthetic protocol turn. Its acknowledgement and notifications confirmed
these shapes, ending in a failed turn without authenticated model inference.
Sanitized evidence is
`tests/fixtures/codex/0.162.0-alpha.17.2/signed-out-live-turn-shapes.json`.

Live alpha envelopes now accept that empty unloaded view and reconcile retained
streamed items at completion. Persisted history still requires full items;
stored summary views or unloaded envelopes containing items remain rejected.

The client previously revoked its account epoch on every `account/updated`
notification, including token/plan refreshes that contain no account identifier.
It now temporarily withholds account readiness and queues scoped traffic and
outgoing application-tool results until a fresh account read verifies the
fingerprint and authentication requirements. The same account preserves the
pending/active turn. Stale account replies and superseded background checks cannot
restore readiness or become chat errors. Confirmed sign-out/account changes,
failed verification, deadlines and queue overflow still revoke scope, discard
unverified traffic and retain uncertain delivery without replay. Out-of-scope
application-tool responses contain only a standardized denial.

All five Debug process/client/controller/UI/tool-runtime suites passed after the
fixes. Client coverage includes unchanged refreshes before/after acknowledgement,
buffered streaming, stale/repeated account events, genuine identity/auth changes,
failed/expired verification, bounded queues, deferred tool invocation/results,
native unloaded live turns and strict stored-history rejection. The integrated
controller regression completes the synthetic send without uncertain delivery.
Three prompt-free native reconnects also passed account/profile/model checks with
Send enabled and no protocol failure; evidence is
`tests/fixtures/codex/0.162.0-alpha.17.2/account-refresh-reconnect-readiness.json`.
Authenticated model completion and the opt-in real-inference suite remain pending.

## Successful completion and current-chat reconnect recovery

The signed-out probe covered failed completion only. Successful completion uses
`itemsView: "summary"` with the final agent message, as shown by the
[upstream app-server implementation](https://github.com/openai/codex/blob/main/codex-rs/app-server/src/bespoke_event_handling.rs)
and its [turn-start regression](https://github.com/openai/codex/blob/main/codex-rs/app-server/tests/suite/v2/turn_start.rs).
This explains the user's full replies followed by an incompatible-turn error.
For exactly the tested alpha, live completed summaries now accept one validated
agent message and merge it with streamed items. Completed-error contradictions,
conflicting completed text, unsafe items, multiple-item summaries, active summaries
and stored partial history remain rejected. The protocol regression covers early
completion and final text supplied through the summary without duplicate items.

Reconnect previously skipped restoration whenever a transcript already existed.
It now resumes the current native binding before considering automatic SQL-based
restoration. This also works with persistence disabled and after an explicit New
Chat has suppressed selection of older saved chats. Native history must contain
the uncertain accepted turn before that warning can clear. Missing/active history,
identity changes and resume failures retain the transcript and do not replay the
prompt or tools. Successful hydration replaces the message cards, clears stale
errors and persists the authoritative terminal status.

A prompt-free native resume of the latest DevHub association also returned
`historyMode: "legacy"` together with the requested complete full-items page.
Alpha resume now accepts that combination as well as paginated history, retaining
the same page bounds, scratch environment, account/home/profile and read-only
scope checks. Regression coverage includes both formats and rejects unknown modes.

The production native probe restored the latest associated completed conversation,
then reconnected that same in-memory conversation and restored it a second time.
The conversation identity and message count were retained, native history showed
the completed user/agent messages, and Send was enabled with no protocol failure.
Sanitized evidence is
`tests/fixtures/codex/0.162.0-alpha.17.2/current-conversation-reconnect-readiness.json`.
The real DevHub database was unchanged; no new prompt or model inference was sent.

Final validation: the Debug application build/link and all five Assistant
process/client/controller/UI/tool-runtime CTest suites passed. The client report
contains 198 passing cases, the controller 41, and the UI 20; optional native test
skips remain. The updated controller regression includes same-account refresh
followed by a successful summary completion. Recovery tests cover persistence on
and off, New Chat suppression, missing/active history, foreign accounts, failures,
and UI replacement without duplicate cards. Authenticated inference remains pending.
