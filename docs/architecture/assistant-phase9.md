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
Authenticated inference and native lifecycle release evidence remain pending,
following the user's earlier instruction to finish offline checks. Production
`liveInferenceApproved()` remains false. Configuration checks and release
approval are separate gates.

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
remain open. Offline coverage does not change production Send admission.
