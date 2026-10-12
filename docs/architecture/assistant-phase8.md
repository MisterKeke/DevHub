# Assistant Phase 8 Report

Date: 2026-10-10

Follow-up: Phase 9 and the three outstanding Phase 8 points are tracked in
`assistant-phase9.md` (2026-10-11). This report describes the earlier Phase 8 run;
its Phase 9-pending statements are historical. Authenticated live gates remain
pending.

## Scope and status

Phase 8 implements the first read-only application-tool path. It composes the
existing Git, workspace, service, Notes, and Files owners through
`AssistantToolRuntime`; it does not add approval flows, mutation handlers, or
MCP. Default CI remains offline and account independent.

The deterministic implementation and protocol checks listed below pass.
Phase 8 is **not release-complete**. Both pinned versions, `0.160.1` and
`0.162.0-alpha.17.2`, now advertise the experimental read tools. The installed
alpha executable regenerated the exact checked-in schemas and passed a native
dynamic-call round trip against a loopback scripted model. Its production
capability admission also passed in an empty temporary Codex home. These are
offline checks, not authenticated model evidence. The user explicitly left
real inference pending. The live harness supports both versions; Phase 6 live
inference and clean-machine gates remain unchecked.

The reported restrictions error was reproduced by the alpha native check:
`unverified-config-source` and `unverified-managed-requirements`. Its ordinary
local `system` layer and `allowedLoginMethods` constraint were being rejected
categorically. Local system layers now undergo the same strict effective-config
checks as other local layers. Login constraints are accepted only when their
known method list permits the independently enforced ChatGPT sign-in. Unknown
sources and all other non-null requirements remain rejected. With those checks
passing, native Send remains disabled by the separate release gate until
authenticated live verification is completed.

## Implementation

- `AssistantToolRuntime` registers nine read-only wire names with explicit
  internal ID mappings: Git status/branches/log, selected workspace metadata,
  merged service definitions/runtime state, note read/search, and file
  read/search. Git uses the existing `GitExecutableLocator`,
  `GitRepositoryLocator`, `GitCommandRunner`, `GitStatusParser`, and
  `GitService` on `TaskScheduler`; repositories above the selected workspace
  are not exposed. Log defaults to 50 and caps at 100.
- Workspace metadata and service definitions are read on their owner thread.
  Service output merges definitions with `ServiceCoordinator::states()` so
  never-started entries appear; only safe IDs, names, lifecycle and health are
  returned. Command and environment values are excluded.
- Note and file body access requires a chat-scoped explicit disclosure grant.
  Note reads use the bounded ownership-checking repository method. Note search
  uses an owner-thread value/path snapshot and the shared `NoteSearchService`.
  File reads use `WorkspaceFileService::readPreview` and preserve the Files UI
  UTF-8/BOM/binary and 1 MiB limits before returning at most 24 KiB. File search
  uses the same bounded, link-pruning service as Files.
- The client validates the pinned tagged function specification, enables
  `experimentalApi` and `thread/start.dynamicTools` only for the two pinned wire
  profile, and folds the tool inventory fingerprint into thread binding.
  Changed inventories and native versions reject persisted-thread resume.
  Inventory fingerprints are independent of the client's previous version.
- `item/tool/call` keeps JSON-RPC request IDs distinct and typed from call IDs,
  validates thread/turn/process association, and returns the fixture-validated
  `success`/`contentItems` response. Registry deduplication executes exact
  duplicate calls once and rejects a reused call ID with changed arguments.
  Cancellation, expiry, lock, workspace changes, and child loss resolve the
  route before late worker results can return.
- Stop and terminal turns now cancel pending reads immediately; new calls are
  denied while cancelling. Cancellation resolves duplicate routes together,
  caches the failed terminal result, and ends the activity indicator once.
  Completion matching includes the full scope, not just the tool call ID.
  Handler registration executes in Release builds as well as Debug builds.
- Native `dynamicToolCall` item notifications and restored history retain only
  the configured tool's item identity. Arguments and output content never enter
  transcript text. Both pinned contract tests cover start/call/completion and
  subsequent history reads with planted private argument/output markers.
- The Assistant page exposes explicit note/file disclosure checkboxes and a
  safe activity indicator limited to internal tool names and terminal status.
  Grants reset on lock/context/access revocation; bodies, arguments and tool
  output are not placed in the activity UI.

## Checklist-to-evidence mapping

| Section 37 Phase 8 point | Evidence and status |
|---|---|
| Register Git reads through existing GitService and scheduler | `AssistantToolRuntime.cpp`; `gitStatusUsesExistingServiceAndRejectsParentRepositoryExpansion` exercises all three read names on a disposable Git repository, compares status/branches/log output with direct `GitService` calls, verifies default/max log limits, and rejects a parent repository. **Implemented and deterministically tested.** |
| Register workspace metadata and service-list reads through existing owners | `ownerHandlersReuseRepositoriesAndStripSecrets` checks workspace repository and service definition repository invocation counts, never-started service state, and planted command/environment secret omission. **Implemented and deterministically tested.** |
| Register scoped notes/files reads and search | `ownerHandlersReuseRepositoriesAndStripSecrets` and `explicitFileDisclosureUsesSharedBoundedService` cover bounded note reads/search and file read/name search through the shared services, including disclosure denial/allow. **Implemented and deterministically tested.** |
| Advertise tagged dynamic-tool specs only in the tested profile | `experimentalDynamicToolCallUsesPinnedSchemaAndTypedRequestIds` runs against both pinned profiles; alpha schema regeneration matches every projection/manifest. `verify-codex-read-tools.py` passed the installed alpha native round trip with a restricted offered surface and no real inference. **Offline native compatibility verified for alpha; authenticated live gate remains unchecked.** |
| Handle `item/tool/call` and return the checked response shape | Client contract test validates request/response shape, integer/string request ID preservation, once-only response, invalid/malformed/stale-scope denial; registry tests cover unknown names, strict arguments, duplicate execution and output caps. **Implemented and fixture-tested.** |
| Tool activity UI and stale registry/profile restore rejection | Assistant UI test covers explicit disclosure controls/activity; the client test `persistedDynamicToolInventoryCannotRestoreUnderDifferentRegistry` rejects stale thread binding. **Implemented and deterministically tested.** |
| Existing capability invocation counts/no duplicate business logic | `allReadHandlersExecuteExistingCapabilitiesOnce` covers all nine handlers on both pinned versions, in-flight/completed aliases, malformed arguments and unknown tools. Owner repository counts and existing GitService command diagnostics prove once-only calls; file/note search handler counts and shared-service output comparisons cover those adapters. Phase 7 shared-service tests also pass. **Read-only scope verified; combined Phase 7–9 checkbox remains unchecked until Phase 9 exists.** |
| Invalid arguments, unknown tools, duplicates and capped output | `assistant-tools` and `assistant-client` cover strict/unknown rejection, typed IDs, call de-duplication/conflict, capped output and malformed envelopes. **Implemented and deterministically tested.** |
| Sensitive-data controls, denial, lock, crash and cancellation | `integratedReadLifecycle` covers files read/search and notes search through production client/controller/registry/scheduler on both versions (42 scenarios): disclosure denial, lock, crash, Stop, disclosure revocation, terminal turn and workspace loss. Queued workers are released after revocation; late results cannot disclose bodies or send duplicate replies. Activity terminates once; locked grants reset; post-Stop calls are denied. Existing bounded note read, account mismatch, secret-stripping and UI disclosure tests also pass. **Integrated offline read lifecycle verified; authenticated live scenarios remain pending.** |

## Verification

Environment: Windows 11 x64; Qt 6.11.0 MinGW shared; GCC/MinGW 13.1.0;
CMake 3.30.5; CTest from the same CMake installation. This is a developer
machine, not the pinned Qt 6.8.3/MSVC baseline or clean-machine evidence.

Application and focused test build:

```powershell
& 'D:\Qt\Tools\Ninja\ninja.exe' -C out/build/windows-dev devhub devhub_assistant_protocol_tests devhub_assistant_process_tests devhub_assistant_client_tests devhub_assistant_controller_tests devhub_assistant_ui_tests devhub_assistant_conversation_repository_tests devhub_assistant_settings_tests devhub_assistant_tools_tests devhub_assistant_tool_runtime_tests devhub_file_browser_ui_tests devhub_notes_search_ui_tests devhub_feature_tests
```

Result: passed, including linking the `devhub` application.

Focused CTest command:

```powershell
$testTemp=(Resolve-Path 'out/build/windows-dev/tests/Debug').Path
$env:TEMP=$testTemp; $env:TMP=$testTemp
& 'D:\Qt\Tools\CMake_64\bin\ctest.exe' --test-dir out/build/windows-dev -C Debug -R 'assistant-protocol|assistant-process|assistant-client|assistant-controller|assistant-ui|assistant-conversation-repository|assistant-settings|assistant-tools|assistant-tool-runtime|file-browser-ui|notes-search-ui|feature-regressions' --output-on-failure
```

Result: 12/12 passed. The temporary directory override is required by this
restricted developer runner; it does not raise any test timeout.

The same 12 suites plus `devhub.qml-runtime` passed 13/13 with `PATH` restricted
to Qt, MinGW, Git and Windows system directories. The installed Codex executable
was absent from that test process PATH, confirming the default regression and
startup checks do not require Codex. Exact isolated-path command:

```powershell
$testTemp=(Resolve-Path 'out/build/windows-dev/tests/Debug').Path
$env:TEMP=$testTemp; $env:TMP=$testTemp
$env:PATH='D:\Qt\6.11.0\mingw_64\bin;D:\Qt\Tools\mingw1310_64\bin;C:\Program Files\Git\cmd;C:\Windows\System32;C:\Windows'
& 'D:\Qt\Tools\CMake_64\bin\ctest.exe' --test-dir out/build/windows-dev -C Debug -R 'assistant-protocol|assistant-process|assistant-client|assistant-controller|assistant-ui|assistant-conversation-repository|assistant-settings|assistant-tools|assistant-tool-runtime|file-browser-ui|notes-search-ui|feature-regressions|qml-runtime' --output-on-failure
```

The QML runtime is not a clean portable package test. `ctest -N` showed no live
test because the opt-in harness is deliberately not registered with CTest.

The opt-in harness compiles successfully and remains excluded from CTest:

```powershell
& 'D:\Qt\Tools\Ninja\ninja.exe' -C out/build/windows-dev devhub_assistant_live_tests devhub_assistant_live_validation_policy_tests
```

It was **not executed**. Running it requires the documented explicit opt-in,
explicit native executable path, a compatible account in an isolated
authenticated disposable `CODEX_HOME` with the sentinel, developer approval for
real inference, and the supported profile. The code refuses the default user
Codex home. Both pinned versions need Git for the disposable read-tool fixture.
No authenticated home, sentinel, or live-inference acknowledgement was supplied
for this run.

`git diff --check` passed. The pinned Windows CI preset still cannot run in this
environment: Qt 6.8.3/MSVC 14.44 is not installed and the available toolchain is
VS 18/MSVC 14.50 with Qt 6.11 MinGW; this is recorded in the Phase 7 report.

## Alpha follow-up verification (2026-10-10)

- Generated stable and experimental JSON schemas from the installed
  `0.162.0-alpha.17.2` native executable in an isolated home. Both projections
  and the manifest are identical to the checked-in alpha fixtures; no fixture
  replacement was necessary.
- `nativeAlphaOfflineProfile`, explicitly enabled with
  `DEVHUB_OFFLINE_CODEX_EXE`, passed through production `CodexProcess` and
  `CodexClient` after the local-system/login-policy fix. It verifies the actual
  version, uses an empty temporary home, and confirms that unauthenticated
  native thread creation remains denied. Default CTest skips this native check.
- `tools/verify-codex-read-tools.py --executable <absolute native executable>
  --version 0.162.0-alpha.17.2 --output <evidence JSON>` passed all six checks:
  isolated home, tagged spec acceptance, one valid call, response consumption,
  terminal completion and a restricted offered tool surface. The sanitized
  evidence is `tests/fixtures/codex/0.162.0-alpha.17.2/offline-read-tool-verification.json`.
  The provider is a loopback scripted fixture, not authenticated OpenAI inference.
- The application, focused regression targets and opt-in live harness were
  rebuilt on Qt 6.11.0 MinGW. The 13 focused Debug CTest suites passed. The
  Release `assistant-tool-runtime` suite also passed, exercising handler
  registration with assertions disabled. The runtime suite includes 18
  invocation-count rows and 42 integrated lifecycle rows across the two versions.
  The default native profile test is intentionally skipped without its explicit
  executable selector (it passed in the separate opt-in run). One existing
  Phase 7 junction-path test was skipped because the restricted runner could not
  create the temporary junction; the new read-tool scenarios were not skipped.
- The separate live-validation policy executable passed all three checks,
  including authorization safeguards for the alpha version. The real-inference
  harness was compiled but not run, per the user's instruction.
- `python tests/codex_contract_test.py` passed 12 tests, including alpha tagged
  contract equivalence and the sanitized native offline evidence. `git diff
  --check` passed.

## Remaining release gates

- Run the opt-in handshake, streaming, Stop, resume, and `devhub_git_status`
  scenario with disposable authenticated homes for every profile for which the
  dynamic tool surface is approved, including `0.162.0-alpha.17.2`. Both pinned
  profiles now advertise the same tagged read-tool inventory.
- Verify the dynamic-tool native capability profile and absence of inherited
  native shell/file/MCP/hook/plugin/browser/computer capabilities in the live
  supported profile. Fixture/schema tests do not establish this.
- Complete clean Windows package checks at the pinned Qt 6.8.3/MSVC baseline,
  including 100/125/150/200% DPI, keyboard use, lock/close transition guards,
  pending Notes/Database changes, services, deadline behavior, helper cleanup,
  and sensitive-data exclusion.
- Complete the combined Phase 7–9 invocation/lifecycle/security checkbox only
  when full read and later-phase behavior has evidence. No Phase 9 feature was
  implemented in this change.
