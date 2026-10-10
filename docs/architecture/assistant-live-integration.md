# Assistant live integration harness

The manual live harness is an explicit developer-only target. It is not added to
CTest and is omitted from normal builds. It launches the production
`CodexExecutableLocator`, `CodexProcess`, `CodexClient`, and
`AssistantController` stack, linked against a separate validation-only build of
the Assistant library. The application and normal test library retain the
production release gate, `CodexRuntimeProfile::liveInferenceApproved() ==
false`. The harness does not create credentials, log in or out, change Codex
configuration, enumerate existing conversations, or save transcript text. Its
repository is an in-memory metadata-only fixture.

## Preconditions and safeguards

- The exact native executable must be provided as an absolute path. The
  production locator and client accept only the tested version (currently
  `0.160.1`). The validation library also requires the executable path to match
  the explicit path environment variable.
- The Codex home must be a separate, developer-created profile directory, not
  the default `.codex` directory or a directory that contains/overlaps it.
- Place a `.devhub-disposable-codex-profile` file in that directory containing
  exactly `DevHub live integration disposable profile` followed by LF. This
  sentinel is an acknowledgement, not proof that the directory has no valuable
  data; verify the disposable home before using it.
- The four exact opt-in environment values and the sentinel are required by the
  harness. The validation library independently checks the build-only policy,
  acknowledgements, explicit executable/version, child `CODEX_HOME`, and
  sentinel before it can admit native inference. After launch, the existing
  client account identity/authentication and effective capability-profile
  checks remain mandatory. The harness never starts login or logout. Do not
  copy the default Codex home or its credentials.
- The harness uses only a newly created thread and fixed prompts: one asks for
  the exact word `READY`; the other asks for a harmless rain explanation and
  interrupts it after the first streamed text delta. It does not use an
  existing conversation or a dynamic tool.
- `CodexRuntimeProfile::liveInferenceApproved()` remains false and is never
  changed by the harness. A compile definition exists only in the separate
  library linked by `devhub_assistant_live_tests`; it cannot be activated by
  environment variables or application settings in production. Capability,
  account, scope, environment and protocol rejection all fail closed.
- Output contains only version, scenario statuses and elapsed time. Transcript,
  account identity, thread IDs, raw configuration and credentials are not
  written to test output or a report file.

## Manual disposable profile setup

Obtain the exact supported native executable and choose a new disposable
directory. Do not reuse or copy the default Codex home. In PowerShell, create
the directory and exact UTF-8, LF-terminated sentinel, then authenticate it
manually in a separate Codex CLI invocation:

```powershell
$codexExe = (Resolve-Path 'C:\path\to\tested\codex.exe').Path
$liveHome = Join-Path $env:LOCALAPPDATA 'DevHub\CodexLive-0.160.1'
if (Test-Path $liveHome) { throw 'Choose a new empty disposable profile directory.' }
New-Item -ItemType Directory -Path $liveHome | Out-Null
$marker = Join-Path $liveHome '.devhub-disposable-codex-profile'
[IO.File]::WriteAllText($marker, "DevHub live integration disposable profile`n", [Text.UTF8Encoding]::new($false))
$env:CODEX_HOME = $liveHome
& $codexExe login
if ($LASTEXITCODE -ne 0) { throw 'Manual disposable-profile authentication failed.' }
```

Review and authenticate only that disposable profile yourself. The command
above does not inspect credentials, and the harness does not run login/logout.

## Build and run

The configured build requires `DEVHUB_BUILD_TESTS=ON`; set the live option only
for a manual developer build. Example for the Windows developer preset:

```powershell
$env:QT_ROOT_DIR = 'D:\Qt\6.11.0\mingw_64'
cmake --preset windows-dev -DDEVHUB_BUILD_LIVE_ASSISTANT_TESTS=ON
cmake --build --preset windows-dev-debug --target devhub_assistant_live_tests
```

Then set the exact acknowledgements and run the target directly. The child
receives only the normal filtered environment plus the isolated `CODEX_HOME`
selector.

```powershell
$env:DEVHUB_RUN_LIVE_ASSISTANT_TESTS = 'I_ACKNOWLEDGE_REAL_CODEX_INFERENCE'
$env:DEVHUB_LIVE_CODEX_EXE = $codexExe
$env:DEVHUB_LIVE_CODEX_HOME = $liveHome
$env:DEVHUB_LIVE_DISPOSABLE_PROFILE_ACK = 'I_CREATED_THIS_DISPOSABLE_PROFILE'
$evidence = Join-Path $env:TEMP 'devhub-assistant-live-result.txt'
& .\out\build\windows-dev\tests\Debug\devhub_assistant_live_tests.exe -o "$evidence,txt"
if ($LASTEXITCODE -ne 0) { throw 'Live Assistant integration test failed.' }
Get-Content $evidence
```

Without the exact opt-in, path, acknowledgement, and sentinel, the test skips
before constructing or launching a child process. With them, it verifies the
native handshake, authenticated ChatGPT account identity, effective capability
profile, and admission before creating its new thread. A rejected or
unsupported profile fails before thread creation. When admitted, it checks
streamed terminal completion, Stop/interruption, resume without duplicate
history, and owned-child shutdown completion within ten seconds. Output is
sanitized and excludes transcript, account identity, thread IDs, raw
configuration, and credentials. The development-validation policy is test
infrastructure only; it is not release evidence or production approval.
