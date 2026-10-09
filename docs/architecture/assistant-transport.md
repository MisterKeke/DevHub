# Assistant process and transport prototype

Phase 1 implements `devhub_assistant` as a Qt Core static library. It contains
application launch/probe values and the replaceable QObject `ICodexTransport` seam,
plus the native executable locator, JSONL codec and `CodexProcess`. The executable
links this target, but bootstrap does not create a backend or enable Assistant.
There is no inference, authentication flow, protocol client or conversation UI in
this phase. Phase 0's rejected release profile remains rejected.

CodexExecutable and CodexLaunchSpec have const value members. They support copy
construction and Qt metatype delivery; fields and whole-value assignment cannot
alter a constructed snapshot. Construct a new value when configuration changes.

## Discovery and launch

The locator accepts a configured native executable path, or resolves `codex.exe`
through PATH when the setting is empty. A bad explicit override fails without
fallback. It rejects directories, relative paths and script wrappers. It runs
`--version` directly through QProcess, accepts only the tested version 0.160.1,
caps each probe channel at 4 KiB, and cancels/kills a probe after five seconds.
Windows launch and version probes use CREATE_NO_WINDOW.
Paths with spaces and Unicode are covered by a copied native fixture. Probe
results carry the caller's generation; cancellation completes child cleanup
before another probe is admitted. No path, credentials or version cache is
persisted. A new backend should request another probe.

The process receives a value snapshot of executable, structured arguments,
absolute scratch directory, explicit environment and generation. It never finds
settings, credentials or a repository directory itself. The caller must create
an empty app-cache scratch directory and pass a validated/admitted launch policy.
This transport's version check is not capability admission. `codexBaseEnvironment`
provides the documented OS plumbing allowlist, excluding keys, arbitrary process
variables and CODEX_HOME. Verified proxy/CA support and validation of an existing
user-selected CODEX_HOME belong to the later admission layer. The native test
probe alone supplies a disposable CODEX_HOME; it never copies authentication.

## Framing and bounded work

Stdout and stderr are separate. The codec accumulates bytes until LF, removes an
optional CR, skips empty records and validates UTF-8 before parsing object
envelopes. Request/notification/response shapes are checked without interpreting
method semantics. Integer IDs in the JSON safe range and string IDs retain their
types. Unknown notification methods are valid. Outgoing objects use compact JSON
and exactly one LF. An incomplete final record is an error.

| Resource | Implementation budget |
| --- | --- |
| Stdout record / pending framing | 4 MiB / 8 MiB |
| Accepted pending stdin, including QProcess bytesToWrite | 1 MiB |
| One native stdin write | Up to 64 KiB |
| Stderr fragment / complete-line diagnostic ring | 64 KiB each |
| One dispatch | 64 records or approximately 4 ms |
| Channel reads | stdout up to 64 KiB; stderr up to 4 KiB per iteration |
| Start deadline | 5 seconds |
| Drain / terminate grace | 2 seconds / 2 seconds, then kill |

The codec budgets its own accumulator. QProcess and the OS also buffer pipe
bytes; their private buffers are not claimed as part of an application-wide hard
memory bound. Both channels are continuously drained in bounded callbacks with
queued continuation, including on exit. One JSON parse/redaction cannot be
preempted, so the time budget is checked between records/iterations. Default tests
include a 3 MiB valid history record and 5,000 records in one burst.

Exceeding the write budget rejects only the new send; accepting bytes does not
acknowledge remote execution. Malformed/oversized stdout fails the session once
and starts asynchronous shutdown. Oversized unterminated stderr is discarded
until LF; only complete redacted lines enter a transient ring. Raw stderr and
protocol payloads are never automatically logged or persisted. Diagnostic previews
are not safe material for automatic support export: generic redaction cannot
recognize every arbitrary secret or identity embedded in free text.

CodexWriteQueue owns the actual transport's byte accounting. It removes only the
accepted prefix and retains every unaccepted byte across short/zero writes,
including split UTF-8 and line delimiters. In-flight Qt bytes count toward the
same cap. Native writes use bounded chunks; bytesWritten continues draining a
remaining suffix. A zero write schedules a 10 ms timer retry, since it may produce
no bytesWritten event. Stop's escalation deadline remains active during retries.

## Lifecycle and ownership

All process handlers are connected before start. `stop()` rejects sends, drains
accepted writes, closes stdin, then escalates through terminate and kill using
timers. It is idempotent. Stop during startup also cleans up a late-starting child.
The final channels are drained before `stopped`; a crash invalidates this session,
and a caller can launch a fresh generation after the terminal signal. Nothing is
replayed. Transport startup ends at the native process's `started` signal;
initialize/account/thread acknowledgement deadlines belong to Phase 2.

The owner must retain the transport and a running event loop until `stopped`,
then quit/destroy. The prototype watchdog exercises this ordering in a separate
application. Emergency destruction kills the direct child before a 100 ms wait;
the watchdog also exercises quitting first with a hung child. This fallback is
not the normal drain path, and cannot guarantee a hard bound if OS termination
fails. Production pre-quit integration into ShutdownCoordinator/MainWindow is
required when bootstrap first owns an Assistant backend; the current application
has no backend to drain.

QProcess owns the direct child, not an arbitrary descendant tree. The cooperative
fake server's owned helper exits on forwarded stdin EOF and is checked for exit.
The opt-in native probe samples Windows parent/child process IDs, holds observed
process handles against PID reuse and checks for survivors after stop. The
containment decision and measured results are recorded below after verification.
No Job Object or process-name-based killing is introduced without a demonstrated
native need. Persistent helpers in excluded/enabled execution, MCP, managed or
attached-runtime profiles remain outside the tested cleanup claim.

## Reproduction

Default tests require only Qt and the project toolchain, with no Codex installation,
network, login, Python or model endpoint. The native helpers are test targets and
are not included in the application package.

```powershell
cmake --build out/build/qml-migration --config Debug --target devhub_assistant_protocol_tests devhub_assistant_process_tests devhub_assistant_native_probe
ctest --test-dir out/build/qml-migration -C Debug -R 'devhub.assistant-' --output-on-failure
```

Use the configured build directory/Qt runtime PATH for another kit. The opt-in
candidate check uses the C++ transport, sends initialize/initialized/config/read,
then stops. It does not start a thread or turn. Select the installed binary; do
not embed an app-managed versioned installation path in production:

```powershell
$candidate = (Get-Command codex.exe -ErrorAction Stop).Source
& out/build/qml-migration/tests/Debug/devhub_assistant_native_probe.exe $candidate
& out/build/qml-migration/tests/Debug/devhub_assistant_native_probe.exe $candidate crash
& out/build/qml-migration/tests/Debug/devhub_assistant_native_probe.exe $candidate kill
```

Tests cover fragmented UTF-8/CRLF, both ID types, invalid envelopes/UTF-8, accumulator
and record caps, truncated EOF, large stderr, write pressure, failed startup,
crash and fresh-generation recovery, stop during startup, hung children,
cooperative helper cleanup and an external application-shutdown watchdog.

## Verification results

The review's immutability and deterministic short-write gaps are resolved.
Compile-time checks require const fields, copy construction and deleted assignment;
runtime checks verify source-value isolation and QVariant delivery. The shared
production queue is exercised with scripted short/zero returns, invalid counts,
write failures and combined queued/in-flight cap checks. An end-to-end fake-server
test bounds every native write to one byte and verifies ordered replies with
fragmented UTF-8 before EOF/stop completes. This forces short acceptance relative
to the queued record without relying on the OS to return a short write.

On 2026-10-09, Windows 11 x64, Qt 6.11.0 and MinGW 13.1:

- The linked Debug application and all Phase 1 targets built successfully.
- All nine CTest suites passed, including existing database, authentication,
  feature, reliability and QML runtime regressions (10.49 seconds for the final run).
- The protocol suite passed 21 checks and the process suite passed 25 checks
  (including data rows and test setup/cleanup). The final-frame test drains 5,000
  records after EOF; the write-drain test receives both accepted 100 KiB requests
  before stop completes. Both watchdog applications exited with no live direct child.
- The existing Phase 0 Python suite passed all 10 checks.
- Native 0.160.1 initialized/read config and stopped with zero survivors in all
  three scenarios: normal EOF drain, direct-child crash via an owned process handle,
  and zero-grace terminate/kill escalation. Each sampled two owned processes;
  crash produced the expected single transport failure. No inference was attempted.

Sanitized native results are frozen in
`tests/fixtures/codex/0.160.1/transport-lifecycle.json`. These observations did not
demonstrate a persistent helper requiring a Job Object, so containment remains
omitted. This sampling detects observed descendants, not helpers that start and
exit between samples or a universal process-tree guarantee. Expanded/admitted
profiles must repeat cleanup tests and add containment if persistent ownership
requires it. Qt 6.8.3, authenticated inference and clean packaged-machine operation
remain untested here.
