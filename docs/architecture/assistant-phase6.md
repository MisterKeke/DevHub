# Assistant Phase 6 Report

Date: 2026-10-10

Phase 6 adds asynchronous shutdown participation, revocable local access, safe
support diagnostics, and release checks. It is **not release complete**. The
unchecked items at the end of Section 37 remain release gates.

## Checklist-to-evidence mapping

| Section 37 item | Implementation and evidence | Status |
|---|---|---|
| Asynchronous pre-quit participant | `ShutdownCoordinator` accepts asynchronous participants, waits in the event loop, and enforces a bounded 10-second deadline. The controller waits for interruption/process stop; `aboutToQuit` is the emergency fallback. The controller test suite includes completion, timeout, and late-completion cases. | Implemented; deterministic test suite passed. |
| Accepted shell close | `MainWindow::closeEvent` retains the service-running guard, then runs Notes and Database transition guards, and requests shutdown only after they accept. The app target built successfully with this route. | Implemented and build-verified; no dedicated close-event UI test. |
| Revoke and interrupt before child stop | The controller increments an access generation and emits `accessRevoked` before interrupting active work. It waits for the terminal event or the existing bounded cancellation timer before calling client stop. No tool registry or approval UI is registered in Phase 6. The controller shutdown ordering test passed. | Implemented and deterministic-test verified. |
| Lock/sign-out | `Locked`, `Locking`, sign-out, and cleared states revoke access, clear the in-memory transcript, cancel probes, and stop the owned backend without a Codex logout request or repository deletion. Controller lock/revocation regression passed; Codex history remains outside DevHub. | Implemented and deterministic-test verified. |
| Lifetime and late callbacks | Shutdown completion follows `backendStopped`; the controller ignores callbacks after shutdown and the coordinator ignores duplicate/late participant completion. Dependencies are stack-owned through coordinator completion. Controller shutdown tests passed. | Implemented and deterministic-test verified. |
| Safe diagnostics and support exclusion | `AssistantController::diagnosticsSnapshot()` exposes only status, operation, tested version/profile and access generation. Support previews explicitly exclude Assistant transcripts and Codex state; exported Assistant/Codex-category log lines and sensitive module-status lines are filtered. | Implemented and support test passed. |
| Planted marker | A unique transcript marker is planted in Assistant/Codex log categories and module status. The ZIP regression asserts that it is absent; the targeted support-bundle test passed. | Implemented and targeted-test verified. |
| Default tests and packaged runtime without Codex | Rebuilt all required Debug targets, then full configured CTest passed all 14/14 suites. Rebuilt the portable Release package and its packaged-runtime checker passed with the child `PATH` restricted to Windows system directories; the checker verified SQLite, Authentication.qml, MainShell.qml, Assistant navigation at module index 9, and packaged runtime. | Verified on Qt 6.11.0 MinGW; checklist complete. |
| Opt-in real handshake/stream/Stop/resume | `tests/assistant_live_integration_test.cpp` remains developer-only and excluded from CTest; live admission still requires the separate validation build, exact explicit executable/version, opt-in, authenticated account, effective capability admission, isolated CODEX_HOME and sentinel. A separate bounded native probe verified version discovery, initialize, config/read and clean stop for 0.162.0-alpha.17.2 in an isolated home, without login or inference. | **Incomplete**: no authenticated handshake, model request, stream, Stop, resume or live shutdown was run. The existing live-inference authorization remains closed and only admits the previously gated 0.160.1 profile. |
| Clean-machine/release gates | Attempted the pinned `windows-ci` configure. Sandboxed configuration was denied while vcpkg extracted tools; the elevated retry installed dependencies and then failed because the configured `Visual Studio 17 2022` generator could not find a VS 17 instance. This machine exposes VS 18/MSVC 14.50, no `QT_ROOT_DIR`, and no Qt 6.8.3/MSVC kit. Windows Sandbox is unavailable. No clean-machine package run, DPI/keyboard check, supported-installation compatibility check, or release shutdown/capability certification was performed. | **Incomplete**. |

## Test and build evidence

- Qt 6.11.0 MinGW Debug build completed for `devhub`,
  `devhub_assistant_controller_tests`, `devhub_assistant_conversation_repository_tests`,
  `devhub_feature_tests`, and `devhub_runtime_tests`, including the final
  repository-save correction and runtime-test temp-path adjustment.
- Full configured Debug CTest passed 14/14 suites with the parent `PATH` set to
  Windows system directories only; `codex.exe` was not discoverable by the test
  processes. Controller tests include
  shutdown ordering, bounded timeout, lock revocation, late callbacks and
  uncertain-delivery coverage. The support-bundle planted-marker regression
  passed as part of `devhub.feature-regressions`.
- The portable Release package rebuilt successfully. Its runtime checker ran
  with `PATH` limited to Windows system directories and passed SQLite,
  Authentication.qml, MainShell.qml, Assistant navigation at module index 9,
  and packaged-runtime checks. The installed Codex executable was not visible
  to the packaged process.
- The supported setup/protocol profiles recorded by this repository are Codex
  0.160.1 and 0.162.0-alpha.17.2. The installed CLI validated for this report
  is `codex-cli 0.162.0-alpha.17.2`. The client hard gate
  At this phase, the production gate remained false. This historical gate was
  superseded by the user's Phase 9 request to enable verified alpha sending;
  see [the current Phase 9 report](assistant-phase9.md). A dedicated
  validation-library target now has a compile-time-only development policy;
  environment variables and settings cannot enable it in production. The live
  target compiled with Qt 6.11.0 MinGW. Its preflight ran with all opt-in
  variables absent: two test fixtures passed and the live scenario skipped
  before child creation. The native alpha probe used an isolated disposable
  CODEX_HOME and confirmed initialize/config-read/stop, with no login or inference.
- The production `devhub_assistant_client_tests` regression passed (CTest
  1/1). It set the four live-test variables to plausible values, verified that
  the normal library's validation-build predicate and native send admission
  remained false after fake authenticated-account/capability setup, and restored
  the process environment. The opt-in-only `devhub_assistant_live_validation_policy_tests`
  passed 3/3 QtTest cases, checking explicit opt-in, exact executable path and
  version, child CODEX_HOME match, and sentinel enforcement using a temporary
  local fixture; it did not launch Codex or authenticate. The real harness
  preflight was 2 passed / 1 skipped because no live opt-in was supplied.
  `ctest -N` still lists 14 default suites and no live target.
- The requested `cmake --preset windows-ci` was retried outside the sandbox.
  vcpkg completed x64-windows dependencies using VS 18/MSVC 14.50, but CMake
  then failed: `Visual Studio 17 2022 could not find any instance of Visual
  Studio`. `QT_ROOT_DIR` is unset; only Qt 6.11.0 MinGW is available, not the
  required Qt 6.8.3/MSVC kit. The clean-machine host, installed supported Codex,
  manual DPI/keyboard review and shutdown release checks remain unavailable.

## Release blockers

1. **Live integration**: obtain the exact live-validation-approved `codex-cli 0.160.1` native
   executable; manually create and authenticate a new disposable CODEX_HOME as
   described in [the harness procedure](assistant-live-integration.md), with
   its sentinel. The existing live harness remains approved only for 0.160.1;
   the alpha setup/protocol support does not authorize real inference.
   Set the explicit inference opt-in, executable path, disposable-home path and
   disposable-profile acknowledgment shown in that procedure. Do not use the
   default user profile or an existing conversation.
   Current compile, offline regression and opt-out preflight evidence do not
   count as a live scenario pass.
2. **Clean Windows release verification**: provision the pinned VS 17 2022 / MSVC
   14.44.35207 runner and Qt 6.8.3/MSVC kit, then run:

   ```powershell
   cmake --preset windows-ci
   cmake --build --preset windows-ci-release
   ctest --preset windows-ci-release
   ```

   Install the resulting release package in a genuinely clean Windows VM with
   no Qt or developer toolchain. Run once with Codex absent, then with the
   supported Codex executable and disposable profile. Record capability and
   account admission, 100/125/150/200% DPI, keyboard-only operation, close,
   lock/sign-out, pending Notes/Database guards, managed services, shutdown
   deadline and owned-child/helper cleanup. Plant sensitive markers and inspect
   the resulting logs and support archive. A development-machine run is not
   clean-machine evidence.

Do not mark Phase 6 complete until both remaining checklist gates have evidence.
