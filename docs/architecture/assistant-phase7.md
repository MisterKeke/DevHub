# Assistant Phase 7 Report

Date: 2026-10-10

## Scope and outcome

Phase 7 adds an internal, injected tool registry and extracts the bounded Files
and Notes read/search behavior used by the UI. The registry permits read-only
effects only. These handlers are not registered in application bootstrap and no
Codex dynamic tools, MCP endpoint, or mutation surface is exposed. The eight
Phase 7 checklist entries in Section 37 are implemented and covered by the
regression evidence below. Phase 6 checklist entries 9 and 10 remain unchecked.

## Implementation

- `AssistantToolRegistry` owns immutable scope snapshots, strict allowed/required
  argument validation, read-only definition admission, bounded concurrency and
  call counts, output caps, monotonic expiry, cancellation, and bounded duplicate
  completion caches. It resolves routes immediately on cancellation/expiry and
  ignores queued worker results after route invalidation or registry destruction.
  Lifecycle hooks cancel scopes on access revocation and calls on child loss.
  Scheduler keys include the scoped process/call identity so unrelated jobs do
  not supersede one another.
- `AssistantToolHandlers` provides an explicitly injected bounded Notes read
  registration helper. It checks explicit note selection/read disclosure and
  delegates to the repository on its owning thread. No repository, QObject, or
  SQL connection is sent to workers.
- `WorkspaceFilePolicy` now owns the path, Windows filename, workspace boundary,
  mutation-target, secret path, and link/junction rules used by the
  `FileBrowserController` compatibility facade and bounded read service.
- `WorkspaceFileService` supplies the UI's asynchronous UTF-8 preview read,
  bounded returned ranges, and cancellation-aware filename/path search. It keeps
  the 1 MiB preview ceiling, UTF-8/BOM/binary behavior, and the search defaults
  of depth 12, 10,000 entries, 2 seconds, and 100 results. Search prunes links,
  junctions, and sensitive/cache directories.
- `application::notes::NoteSearchService` receives only an authorized value
  snapshot and cancellation token. `NotesPage` delegates its content scan to the
  same service; the existing title/tag matching remains in the page. Each source
  is capped at 2 MiB and invalid UTF-8, binary content, and link ancestry are
  rejected. Search jobs are coalesced and stale generations are ignored.
- `INotesRepository::readContentBounded` and `NotesRepository` enforce account,
  workspace, note-kind, non-trash ownership, UTF-8/BOM/binary checks, link
  ancestry, and a hard 2 MiB maximum before returning note content.

## Checklist-to-evidence mapping

| Section 37 Phase 7 checklist point | Implementation and verification evidence |
|---|---|
| ToolDefinition/Call/Result/Scope and strict validators | `AssistantToolRegistry.h/.cpp`; `registryStrictArgumentsAndReadOnlyAdmission` covers unknown/forged account and root keys, type failures, mutation rejection, and minimum output admission. |
| Admission, capped output, expiry, duplicate handling | Registry bounded call/concurrency admission and terminal cache; `registryDuplicateExpiryCancellationAndStaleCompletion` and `registryDeduplicatesCompletedCallAndCapsOutput`. |
| Route cancellation before stale completion | Cancellation/expiry resolve route and signal the shared cancellation token before callback; scope/process cancellation and late callback are covered by the registry tests. |
| Shared WorkspaceFilePolicy | `FileBrowserController` delegates each retained policy operation; `filePolicyRejectsTraversalAdsAndSecrets` and link/junction test. |
| Shared bounded preview | `FileBrowserPage` uses `WorkspaceFileService::readPreview`; `filePreviewUtf8BomBinaryLimitAndRanges` plus Files UI preview regression. |
| Bounded filename/path search | `searchNames` applies the plan budgets and cancellation; `filenameSearchBoundsPrunesSecretsAndCancels` covers defaults/caps/path matching/pruning/cancellation. |
| Extracted Notes content search | `application::notes::NoteSearchService` is used by `NotesPage`; source cap/cancellation/link tests and Notes UI title/content/clear regressions. |
| Bounded note reads with repository ownership | Injected handler spy confirms a single delegated bounded read and explicit disclosure denial; repository-backed test verifies wrong account/workspace, trashed note, and byte cap rejection. |
| Extracted Files/Notes UI behavior | `file-browser-ui` verifies the shared UTF-8 preview result; `notes-search-ui` verifies content-only filtering, title filtering, and clearing the filter. |
| Phase 7 path/security/cap/cancel cases | `assistant-tools` verifies traversal, ADS, secret paths, a real junction, repository ownership, 1 MiB/2 MiB limits, bounded ranges, cancellation, expiry, and stale callbacks. |

The Phase 7 UI-preservation and security subset is now checked separately in
Section 37. The Notes handler spy verifies one bounded repository invocation,
and the Files/Notes UI use the extracted shared implementations. The broader
cross-phase checks remain unchecked: complete invocation accounting for
Git/workspace/service handlers, plus lock/crash and no-mutation-replay scenarios
depend on later-phase handlers and lifecycle scenarios that are not claimed.

## Verification

Environment: Windows 11 x64; Qt 6.11.0 MinGW shared; GCC/MinGW 13.1.0; CMake
3.30.5; CTest from the same CMake installation. This is a developer environment,
not clean-machine release evidence.

Build command:

```powershell
& 'D:\Qt\Tools\CMake_64\bin\cmake.exe' --build out/build/windows-dev --config Debug --target devhub_assistant_tools_tests devhub_file_browser_ui_tests devhub_notes_search_ui_tests devhub_assistant_conversation_repository_tests devhub_assistant_controller_tests devhub_feature_tests
```

Result: passed. The production Assistant static library and modified Files,
Notes, and repository sources were compiled as dependencies of the selected
targets.

Focused CTest command:

```powershell
& 'D:\Qt\Tools\CMake_64\bin\ctest.exe' --test-dir out/build/windows-dev -C Debug -R 'assistant-controller|feature-regressions|assistant-tools|file-browser-ui|notes-search-ui|assistant-conversation-repository' --output-on-failure
```

Result: 6/6 passed (`assistant-controller`,
`assistant-conversation-repository`, `assistant-tools`, `file-browser-ui`,
`notes-search-ui`, and `feature-regressions`). The controller and planted-marker
support-bundle checks revalidate the implemented Phase 6 lifecycle/diagnostics
foundation used here.

The junction-enabled direct test command was:

```powershell
& '.\out\build\windows-dev\tests\Debug\devhub_assistant_tools_tests.exe' -o '.\out\assistant-tools-junction-result.txt,txt'
```

Result: 12 passed, 0 failed, 0 skipped. This includes a real temporary Windows
directory junction fixture. `git diff --check` passed.

The pinned Windows CI configure command was attempted:

```powershell
$env:QT_ROOT_DIR = 'D:\Qt\6.11.0\msvc2022_64'
& 'D:\Qt\Tools\CMake_64\bin\cmake.exe' --preset windows-ci
```

It could not configure because the preset requires Visual Studio 17 2022 and
toolset v143 14.44.35207, while this machine has Visual Studio 18 / MSVC
14.50.35717. The pinned Qt 6.8.3 MSVC kit is also not installed; this machine's
available configured kit is Qt 6.11.0 MinGW. Therefore the Windows CI build/test
preset remains unverified here. The full `devhub` MinGW link is separately
blocked by the installed libsodium 1.0.22 MinGW archive's unresolved
`memset_explicit` symbol; the targeted production library and Phase 7 test
targets build successfully.

## Deferred gates

- Phase 6 real handshake/stream/Stop/resume tests and clean-machine
  compatibility/capability/shutdown checks remain unchecked as explicitly
  deferred release gates.
- Phase 8/9 checklist entries remain unchecked. No Codex dynamic tools or MCP
  integration was added.
- The combined Phase 7–9 capability-count and lifecycle/security checkboxes in
  Section 37 remain unchecked until their full Git/workspace/service, lock,
  crash, and mutation-recovery scope has evidence.
