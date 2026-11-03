# Build & Test Center

**Status:** Local workspace implementation · **Reviewed:** 2026-11-03

## Scope and execution model

Build and test operations are short-lived jobs. `BuildExecutionCoordinator` resolves an executable
and an argument list, launches it asynchronously through the existing process launcher, streams its
output and owns its timeout/cancellation state. It does not register jobs with `ServiceCoordinator`:
services have restart and health-monitoring lifecycles that do not fit one-shot builds. The existing
managed-process implementation still owns the Windows process tree. Only one Build & Test job runs
at a time.

Profiles, runs and parsed test rows are stored in SQLite under both the local account and workspace.
Planner tasks can reference one build profile and one test profile. An explicitly started run keeps
the task ID in its history row and publishes a `buildtest.completed` Planner activity event. Opening,
importing or completing a task never starts a command, and a successful run never completes a task.

## Runner support

| Runner | Implemented behavior | Limits |
| --- | --- | --- |
| CMake configure/build | Uses detected configure, build and test preset names with CMake's native preset commands. Fallback build and CTest profiles include Debug and Release configurations when the configure preset's binary directory is resolved. | Detection reads the two top-level preset files and resolves inheritance between them for configure `binaryDir`/`generator` and build/test preset associations. Included preset files and preset environment macro expansion are not modeled; a preset whose output directory cannot be resolved inside the workspace is not runnable from DevHub. CMake itself still receives the native preset and applies its own semantics. |
| CTest | Runs a selected test preset or a build-directory/configuration pair; requests JUnit XML, presents test cases and supports a selected-test or failed-test rerun filter. | Preset conditions are left to CMake/CTest to evaluate at launch. |
| Go build | Runs `go build` from the validated workspace working directory. | No install or download step is performed. |
| Go tests | Runs `go test -json`, displays package/test events, supports package/flag arguments, `-run` selection, and an optional `-race` profile setting. | The race checkbox is available when Go markers are detected; DevHub does not probe CGO support before saving it, so an unsupported toolchain reports its own failure. |
| pytest | Uses the workspace `.venv` Python executable when present, or a configured Python executable, and requests JUnit XML. Supports failed-test rerun through pytest's cache and selected-name execution. | DevHub does not install pytest or other Python packages. pytest must already be available in that environment. |
| npm script | Reads `package.json` script names and runs only a script selected in a saved profile. The user explicitly marks a profile as a test action. | Output is retained as a test job; DevHub does not infer per-test results from text. Windows `npm` launcher execution depends on the locally resolved npm executable and was not integration-tested here. |
| Make target | Runs a user-entered target only after validating the target syntax and resolving Make. | The Make runner has no automatic target selection. |
| Custom command | Runs an explicitly configured executable with separate arguments and an in-workspace working directory. | Per-test results and environment overrides are not provided. |

For CMake, CMake owns the meaning of detected presets, including build preset configuration. CMake
documentation specifies that build and test presets infer their build directory from the associated
configure preset, and that preset inheritance can supply fields such as `binaryDir`.
[CMake Presets reference](https://cmake.org/cmake/help/v3.28/manual/cmake-presets.7.html#configure-preset)

## Safety, output and lifecycle

- Executables and arguments are passed separately with shell mode disabled. Working directories,
  build directories and editor file paths are resolved and constrained to the active workspace.
- The first run of an untrusted profile shows the resolved executable, arguments and directory.
  The user can run once or save trust for that profile in that workspace. Project scripts remain
  executable code; profile selection alone does not run them.
- Profiles do not store custom environment overrides. CMake's native preset file can still supply
  the environment that CMake itself uses. Sensitive environment values are not copied into DevHub
  profile storage.
- Workspace changes and account locking/sign-out request cancellation. Shutdown waits for the
  managed process tree to finish its cancellation path. Timeouts, cancellation, launch failure and
  process exit are stored as distinct statuses.
- The active output view keeps at most 2 MiB, and clearly marks omitted earlier text. Per-run logs
  are capped at 4 MiB and retain the newest 100 log files. Go structured input is capped at 8 MiB;
  a truncation notice is stored with the result so partial parsed results are not described as a
  complete pass. JUnit reports are removed after parsing and old reports are pruned to 100 files.
- SQLite retains the newest 100 runs per workspace, with bounded summaries and test detail. Logs
  are separate files and are not copied into the database.

## Planner, palette and notifications

Planner's Development section provides separate build and test profile selectors. Its Build
Project, Run Associated Tests and View Last Test Result actions require a click and verify the
active workspace/profile association again before acting. Completed associated runs add structured
status, runner, duration and test-count metadata to the task's development activity timeline.

The Command Palette exposes Build current workspace, Run tests, Open Build & Test Center, Cancel
current job and View failed tests. Completion/failure notifications are posted once per run; user
cancellation does not produce a success/failure notification.

## Assistant integration point

There is no Analyze Failure with Assistant button. The current Assistant approval flow does not
provide a reviewed payload boundary for selected build output. A future action belongs beside the
selected run/test details in `BuildTestPage`: construct a bounded, redacted excerpt, show the exact
excerpt and destination to the user, then submit only after explicit consent through the existing
Assistant UI/API. It must not add build output to context or launch generated commands implicitly.

## Verification coverage

`devhub_buildtest_tests` covers marker detection, preset inheritance and missing build-preset
semantics, Go/CMake test parsing, diagnostics, success/cancellation/timeout/workspace-change
behavior, log truncation and account-scoped SQLite history. `devhub_planner_ui_tests` covers task
profile persistence, explicit Planner actions and the completed-run activity event. Database
migration and QML runtime tests cover the new schema and navigation/page resources. No network
download or real toolchain is required by these unit tests.
