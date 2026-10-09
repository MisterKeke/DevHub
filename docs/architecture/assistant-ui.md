# Assistant page and streaming controller

Phase 3 replaces Quick Ask with a QWidget Assistant inside the existing module
dialog. Index 9, `window/lastPage` restoration, command palette access and the
QML shell's module-index mapping remain intact. `nav.assistant` replaces
`nav.quickAsk`; command recents remain in memory. A separator above Assistant
does not add a selectable row. Ordinary Send has no browser or external-chat path.

## Composition and state

Bootstrap owns one native transport, typed client, locator and controller for the
DevHub session. The controller survives hiding the module dialog. Opening
Assistant starts an asynchronous PATH/version probe; threads are created only
when an admitted message is submitted. An empty `QTemporaryDir` under the app
cache is the process cwd. The environment and arguments use the Phase 2 allowlist
and canonical restricted profile; no credentials or user configuration are copied.
Executable selection/settings and sign-in UX remain Phase 4 work.

The application-facing `ICodexClient` port keeps infrastructure headers out of
the controller. It contains typed protocol operations/signals, not an additional
fake backend. Controller/UI tests use the real `CodexClient`, frozen 0.160.1
contract and shared `MemoryCodexTransport`.

Connection state is distinct from conversation operation. Protocol connection
does not mean permission to Send. Admission additionally requires account/profile
checks, a text model, an unchanged context and the client inference gate. The
native gate remains closed. There is no development flag that bypasses it in the
page/controller; no native sign-in or model turn was performed for Phase 3.

The controller owns conversation/message UUIDs, UTC timestamps, source text,
opaque thread/turn/item associations and delivery state. The page owns documents
and card widgets only. `IConversationRepository` declares metadata-only storage;
bootstrap supplies null until Phase 5. Listing saved conversations returns a
typed unavailable result rather than opening another database.

Locking stops the backend and preserves transcript/draft behind the existing
opaque lock UI. Sign-out clears the controller transcript and page draft. Workspace
changes interrupt an active turn, retain the original context and require New
Chat. Context labels do not grant native workspace access. Pre-quit integration
starts process stop; the existing Phase 1 process watchdog remains the fallback.
Full shutdown-coordinator integration remains Phase 6 work.

## Admission, streaming and recovery

The composer stays intact while creating a thread. It clears only after
`turn/start` is queued, and only if its current text still matches the admitted
source. Editing a new draft during admission cannot erase that draft. Thread or
local write failures retain the source and show an inline safe error.

One operation/turn is active at a time. Limits are 64 KiB UTF-8 per user message,
2 MiB per assistant message, 8 MiB transcript text and 200 cards. Multiple agent
items get separate message UUIDs; reasoning is not shown. Changed items are
coalesced every 50 ms and flushed at item/turn completion. Canonical final text
replaces streamed text. Cap breaches retain a UTF-8-safe prefix, mark it truncated,
disable further Send and interrupt the actual turn. The bounded protocol state
continues to be consumed, including terminal events.

Stop uses the client's accepted thread/turn IDs. Before an ID is accepted it
closes the connection rather than inventing an interrupt ID. A five-second
cancellation watchdog closes a backend that never reaches terminal state.
New Chat during generation waits for cancellation/completion before clearing the
view and allocating a new conversation; its next Send creates a new thread.
Prior associations are retained as bounded metadata in the controller for the
current DevHub session (up to 100), without transcript bodies or another
repository. Sign-out clears them. Neither action deletes Codex's durable history.

Copy writes the message source only on an explicit click. Retry of a demonstrably
unsent message is an explicit Send. Accepted failed/interrupted messages first
request scoped `thread/read`; only a confirmed terminal turn enables a separate
"Send again as new turn" click. Checking history does not send. Unknown delivery
is not automatically replayed. Reconnect retains the transcript; an old accepted
conversation without a current binding requires New Chat in this session-only
MVP. Restoring accepted conversations across backend restarts belongs to Phase 5.

## Rendering and interaction

`SafeMarkdownBrowser` replaces Notes' private preview. Notes keeps its disabled
link behavior and existing qrc-only resource policy. Assistant blocks every
resource at the QTextDocument boundary as well as the browser boundary: returning
null only from QTextBrowser is insufficient because Qt can fall back to a
file/data loader. Remote images, file URLs, data resources and arbitrary qrc
assets cannot be loaded into an Assistant document.

Assistant Markdown uses GitHub syntax with `MarkdownNoHTML`; raw tags remain
literal and fenced code is selectable. User messages use plain text. Automatic
link opening is disabled. An explicit anchor click can open HTTP/HTTPS with a
valid host and no userinfo; file, data, javascript, qrc and other schemes are
rejected. No scoped Files link action is implemented yet.

Selection anchor/position integers and document scroll offsets are captured
before document replacement. Changed cards alone are rerendered. Scroll follows
when already near the bottom and no message selection is active; otherwise
position is preserved and Jump to latest appears. Deferred scroll work is
coalesced and invalidated on transcript reset. Large cards use a bounded inner
browser height, with the outer transcript in a QScrollArea.

Enter sends, Shift+Enter inserts a line, auto-repeat cannot duplicate Send, and
input-method preedit suppresses Send until composition is committed. Buttons and
composer have accessible names and keyboard tab order. Rendering uses no
animations, so reduced-motion mode adds no special streaming effect.
Tab and Shift+Tab move focus out of the composer; the composer does not insert
tabs. Keyboard tests exercise focus traversal, Enter admission, auto-repeat
suppression, Stop and New Chat with Space, and input-method preedit/commit.

## Verification and remaining release checks

The controller suite covers lazy startup, admission, duplicate sends, source/final
reconciliation, separate items, hidden reasoning, all four caps, Stop/final/New
Chat races, explicit history-before-retry, crash delivery, workspace/session
revocation, cancellation watchdog and unavailable persistence. The UI suite covers
draft preservation, concurrent composer edits, Enter/Shift+Enter/preedit, document
resource denial, HTML/code/plain text, selection, Copy, scroll/Jump to latest,
input validation and the production command registration/session-only recents.
QML runtime checks require exactly one Assistant delegate at index 9. Existing
Notes reliability tests exercise the extracted widget with the real NotesPage.

Verified locally on 2026-10-09:

- The full Debug application build and all 12 CTest suites pass with Qt
  6.11.0/MinGW, including 19 controller checks, 14 UI checks and 75 typed-client
  checks (these totals include QtTest setup/cleanup). Existing Notes and QML
  runtime regression suites also pass.
- All 10 Python frozen-contract checks pass; candidate wire snapshots are
  unchanged.
- The UI suite passes at 100%, 125%, 150% and 200% scaling. Screenshots in
  `out/assistant-ui-verification/scale-{100,125,150,200}.png` exercise the real
  theme, multiline composer, Markdown/code and literal raw tags. Scroll tests
  verify both the scrollbar value and actual transcript viewport movement;
  selection and deliberate scrolling survive final-text reconciliation.
- New controller, page, Markdown browser and controller/UI test sources compile
  with MSVC 14.44, `/O2 /W4 /WX`, against the local Qt headers. This is a source
  compile check; it does not link or run a Qt/MSVC application.

The last Phase 3 checklist item is now verified against the actual Qt 6.8.3/MSVC
runtime as well:

- The official `qtbase` SDK was extracted into `out/assistant-baseline/qt`,
  without changing the installed Qt kit. Its published SHA1 is
  `ab0bd2551618f7128ef30c6d74fe45b6a76e2296`. Package metadata is from
  [Qt's official repository](https://download.qt.io/online/qtsdkrepository/windows_x86/desktop/qt6_683/qt6_683/qt.qt6.683.win64_msvc2022_64/).
- An isolated Release harness builds the production core/Assistant source lists
  from the root CMake file, the real page/theme/browser and the existing test
  suites with Qt **6.8.3 EXACT**, MSVC 14.44 and warnings as errors. Both suites
  pass: 14 UI checks and 19 controller checks. The QtTest logs confirm the loaded
  Qt 6.8.3 runtime, rather than only a compile against its headers. This focused
  harness does not constitute a full-application Qt 6.8.3 build.
- All 14 UI checks pass at 100%, 125%, 150% and 200% scaling on both kits.
  Baseline screenshots/logs are in `out/assistant-baseline/verification` and were
  visually inspected. Control geometry and label-width assertions detect clipped
  composer/buttons in the tested 900×700 logical layout. Rendering uses Qt's
  offscreen platform; keyboard and IME events are deterministic QtTest events.
- `tools/verify-assistant-ui.ps1` repeats this matrix, checks the actual runtime
  version, rejects failures/skips, writes snapshots and restores its environment.
  The Windows CI workflow now runs it against its normal Qt 6.8.3 Release target
  and uploads the synthetic snapshots and logs.

To repeat the local baseline matrix with the extracted kit:

```powershell
./tools/verify-assistant-ui.ps1 -Executable out/assistant-baseline/build/devhub_assistant_ui_tests.exe -QtRoot out/assistant-baseline/qt -OutputDirectory out/assistant-baseline/verification -ExpectedQtVersion 6.8.3
```

All 20 Phase 3 implementation checklist items are checked. Real Windows IME and
screen-reader review remain supplemental manual release checks; the deterministic
checks do not certify those OS interactions. The harmless authenticated turn
still requires the Phase 2 profile/model gate and explicit development opt-in.
No native inference or GitHub access was performed.
