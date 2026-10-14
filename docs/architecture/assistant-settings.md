# Assistant setup and authentication

Phase 4 adds an Assistant tab to both DevHub Settings entry points. The
controller and typed client own runtime state; widgets display snapshots and
request explicit actions.

## Preferences and applying changes

The global preferences `assistant/executable` and `assistant/model` are empty
by default and classified Sensitive. Support settings exports exclude them.
`assistant/restoreConversation` defaults to true and controls whether DevHub
saves and restores local conversation associations. Turning it off stops new
association writes and automatic resume; it does not delete saved metadata or
Codex-owned history.
There are no API key, token, authorization URL, device code, credential-home or
runtime-status settings.

An empty executable preference discovers native `codex.exe` on PATH. A nonempty
preference must be an absolute `.exe` path with bounded, control-free text;
quoted commands, script wrappers and directories are rejected. A missing
absolute file can be saved for later installation, but connection/recheck reports
it as invalid and does not fall back to PATH. Corrupt stored Assistant values
become an invalid sentinel instead of silently choosing PATH or another model.

Browse selects a native executable. Recheck asynchronously probes the current
draft using the bounded version check. It does not save, connect or sign in.
Its independent locator cannot interrupt the active Assistant backend. Edits and
dialog closure cancel the owned probe; stale generations cannot replace status.
Immediate recheck after cancellation waits for the previous child to exit.

Save validates all controls, then writes persistent settings in one repository
transaction. Session-only values enter memory only after that transaction
commits. Theme and Assistant runtime changes and `settingsApplied` are published
only after success. Both settings entry points use that hook to reload Assistant
preferences. A failed batch leaves persisted preferences, session values and
active runtime configuration unchanged. Cancel does not save the draft.

Preferences set before first connection preserve lazy startup. An opened
Assistant reconnects on idle changes; changes during a turn, history check or
sign-in wait for completion. Latest pending preferences win. Reconnect preserves
the transcript and composer; it never replays a message. On a new app session,
Phase 5 resumes only compatible conversations associated by DevHub. An unavailable
or incompatible association requires New Chat.

The saved runtime profile fingerprint includes the Codex version. Switching
between supported versions therefore does not silently resume a conversation
under a different runtime contract.

## Status and models

Version, process/connection state, account mode and restrictions are transient
controller values. Installation/probe failures, disconnected/starting/running,
signed out, ChatGPT, unsupported API-key/other accounts and expired sign-in are
distinct. Raw account identifiers, credential paths, email and server diagnostics
are not displayed.

DevHub supports the tested native `codex-cli 0.160.1` and
`codex-cli 0.162.0-alpha.17.2` profiles. Version output accepts semantic
prerelease/build suffixes and preserves the complete detected version; a valid
but untested version is rejected with that version shown in the setup error.
The protocol contracts embedded in DevHub are generated from each supported
executable's stable and experimental `app-server generate-json-schema` bundles.
The 0.162.0 alpha schemas retain the requests and responses DevHub uses. Their
differences are additive optional `supportsIndependentSpeedModes`, `parentTurnId`
and `rootTurnId` fields; DevHub ignores these additions and continues validating
its required fields, so no request adaptation was needed.

Model choices come from the tested `model/list` response's first 100 entries.
Hidden/non-text entries are omitted; duplicates are collapsed and availability
notices conservatively disable choices. Missing/unavailable saved choices stay
visible and prevent Send. Automatic selects an available default text model.
The catalog never proves entitlement or opens the native inference gate.
Successful authorization refreshes the account and then the catalog.

## Explicit authorization

Connect ChatGPT and Connect with device code use the pinned client flows only
while DevHub is unlocked, idle, initialized and the restricted profile is
verified. Unsaved executable changes disable Connect/reconnect until Save.

The panel explains that Codex manages a shared ChatGPT sign-in; completing
authorization may change the account used by other Codex apps. DevHub requests
no API key, reads no credential files, provides no global logout and persists
no credentials. This is separate from DevHub's local account/password.

Requesting a challenge does not open a browser. Open authorization page is a
separate explicit click, revalidated against the client's shared policy: HTTPS,
pinned authorization hosts/paths, no userinfo/fragment/nonstandard port, at most
8192 URL bytes, and the exact query-free device verification endpoint.
Only the transient URL/device code are displayed; login IDs are not displayed.
Challenges are not written to settings, logs, notifications or support bundles.

Cancel clears the challenge immediately and sends its actual login ID. Late
completion cannot revive it. Cancellation before its ID is known closes the
backend. Closing the panel cancels a sign-in it started. Expiry, completion,
connection loss and local lock/sign-out clear transient values. Authorization
alone does not enable native Send.

## Verification

The settings suite covers sensitivity/defaults, space/Unicode overrides, corrupt
saved values, async draft recheck/stale results, deferred settings, failed saves,
Cancel, both challenges, explicit browser opening, invalid URL denial, pending
cancellation on close, expiry, auth modes, empty/unavailable catalogs and the
real settings dialog. Process tests use an owned native helper for cancel/recheck.

Verified locally on 2026-10-09:

- Full Debug application build and all **13 CTest suites** pass on Qt 6.11.0/MinGW.
  The new settings suite has **19 passing checks**, including setup/cleanup; the
  native process suite also passes its cancellation/replacement-helper test.
- The focused Qt **6.8.3/MSVC 14.44 Release** harness builds with warnings as
  errors and passes the chat, controller and settings suites. This verifies the
  real baseline runtime without claiming a full-application baseline build.
- All 19 settings checks pass at **100%, 125%, 150% and 200%** scaling on both
  Qt kits. Idle and synthetic device-challenge screenshots are retained under
  `out/assistant-baseline/verification/settings` and
  `out/assistant-ui-verification/settings`. The baseline renders were visually
  inspected; CI repeats the matrix and uploads snapshots/logs.

All eight Phase 4 implementation checklist items are checked. Sign-in fixtures
are synthetic and use the real typed client through in-memory transport. No real
authorization, shared-credential mutation, native inference or GitHub access was
performed. Real account authorization remains an explicit user action; the
existing native inference release gate remains closed.
