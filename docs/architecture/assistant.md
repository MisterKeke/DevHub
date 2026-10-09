# Assistant: Phase 0 contracts and capability evidence

Evidence date: 2026-10-09. This phase freezes the integration contract and records
release gates. It does not implement or enable Assistant inference.

Follow-up verification: [assistant-capability-verification.md](assistant-capability-verification.md)
records 27 native scenarios and ten passing derived checks. It resolves the native
execution/environment questions for a bounded profile and tests inherited MCP,
plugin and hook controls. Unavailable managed/browser/computer integrations and
live ChatGPT/model verification remain excluded. The original profile stays rejected.

## Architecture baseline

MainWindow.cpp constructs Widgets feature pages and owns the nonmodal module
QDialog. It embeds MainShell.qml with QQuickView/createWindowContainer. QML's last
visible navigation entry maps to index 9, currently Quick Ask; Settings remains
index 8 outside the repeater. The later Assistant replaces index 9 and migrates
nav.quickAsk to nav.assistant, preserving last-page restoration.

The composition root remains ApplicationBootstrapper. Codex will be a dedicated
protocol child, not a project service in ServiceCoordinator. Use an empty cache
scratch directory. No process, protocol, UI, database, authentication or packaging
production code changes are part of Phase 0. See [ADR 0006](../decisions/0006-assistant-app-server-and-hybrid-ui.md).

## Candidate and compatibility table

PATH resolved a native codex.exe and returned `codex-cli 0.160.1`. Its observed
desktop-managed version directory is machine-specific and is deliberately absent
from fixtures and launch policy. The plan's 0.160.0 version is historical evidence,
not a minimum supported version.

| Version / profile | Evidence | Inference / restoration eligibility |
| --- | --- | --- |
| 0.160.0, planning snapshot | Previously inspected help/experimental schema; not regenerated or runtime-tested here | Unverified; not supported by this phase |
| 0.160.1, stable connection with candidate overrides | Handshake, config and inventories work; thread/start with environments=[] returns -32600 | Rejected; required scope control unavailable without experimental opt-in; stable account/read lacks routing identity |
| 0.160.1, experimental connection with candidate overrides | Empty selection accepted; inherited MCP remains active; native execution is absent in follow-up tool-surface/dispatch tests despite unified_exec inventory reporting true | Rejected; inherited MCP and goal tools remain; no inference or automatic resume enabled |
| 0.160.1, strengthened bounded development profile | Individual MCP disablement, disabled plugins/hooks/goals/image generation, and empty thread/turn environments pass synthetic controls | Development contract only; attached browser/computer runtimes, managed hooks/conflicts, unverified model catalogs and live ChatGPT path excluded |
| 0.160.1, Phase 2 typed-client development profile | Frozen-schema validation, account/profile admission, thread/turn races and deny replies tested with an in-memory transport | Synthetic turns only; native Send remains blocked pending authenticated/model release gates |
| Any other version | No evidence | Unverified; fail closed until explicitly rechecked |

These are tested **candidate** profiles, not a claim that every possible 0.160.1
configuration is unsafe or unsupported. No profile is approved for release yet.

## Frozen artifacts

[tests/fixtures/codex/0.160.1](../../tests/fixtures/codex/0.160.1) contains:

- `cli-evidence.txt`: exact version and App Server/schema-generator help output.
- `stable.json` and `experimental.json`: selected request envelopes, notifications,
  all server-request envelopes, response schemas and their complete reference
  closure. Shared definitions are deduplicated. Descriptive schema annotations
  are removed; wire property names, required fields, types, enums and constraints
  remain. These are DevHub fixture containers, not stand-alone JSON Schema roots.
- `manifest.json`: SHA-256 of canonical JSON for every generated file and each
  projected fixture. Stable generation produced 314 files; experimental generation
  produced 440. Full CLI-generated snapshots remain in ignored `out/assistant-phase0`.
- Three inventory JSON files: stable, experimental, and seeded adversarial runs.
  They contain selected boolean/count evidence, no raw responses, paths, account
  identity, hook commands, transcript or credentials.

The generator itself is experimental tooling even when its output filters out
experimental API fields. The schema is shape evidence, not runtime permission or
entitlement evidence. In particular, the CLI rejects `approval_policy="untrusted"`
at startup even though that enum remains in the generated schema. The candidate
uses `on-request`; a future client must deny disabled native approvals.

## Required wire contract

Messages are compact JSON objects separated by LF, without a jsonrpc header. IDs
may be int64 or strings; preserve their type. Client and server request IDs have
separate correlation spaces. `configRequirements/read` takes null params (or an
omitted params field), not an object. Most other selected requests require params.

| Flow | Required contract / handling |
| --- | --- |
| initialize -> initialized | clientInfo.name/version; capabilities.experimentalApi opts in. Result requires userAgent, codexHome, platformFamily, platformOs. Send initialized only after success. It is not a complete method/tool inventory. |
| account/read | requiresOpenaiAuth; optional nullable account. ChatGPT variant has type, nullable email and planType. workspaceRouting is experimental in this candidate. Null/non-ChatGPT account cannot authorize chat. |
| account/login/start, cancel | Tagged ChatGPT browser/device-code request and response shapes are captured; URLs/codes remain transient. Token-import/provider variants in the schema are outside DevHub policy. No real login was tested. |
| model/list | Catalog schema captured; does not prove entitlement. Not called during this probe. |
| thread/start | Result contains thread.id and resolved settings. environments and dynamicTools require experimental opt-in; no dynamic tools are advertised for text-only MVP. |
| thread/read, resume | Required threadId; read can includeTurns. Resume has no dynamicTools parameter. legacy/paginated history enums exist experimentally; bounded legacy behavior still needs runtime verification. |
| turn/start, interrupt | Start requires threadId/input, including tagged text input. Interrupt requires threadId/turnId. No turn was started here. |
| item/started, agentMessage/delta, item/completed | Route by threadId/turnId/item identity. Delta requires threadId, turnId, itemId, delta. Completed agent item has id/type/text. Reconcile final text instead of appending twice. |
| turn/completed | Terminal status is completed, interrupted or failed; inProgress is nonterminal. Inspect error fields. |
| server requests | Native command/file approvals return decision envelopes; permission, user-input and MCP elicitation have distinct shapes. Dynamic tool result requires contentItems/success, not a native approval decision. Unknown/disabled methods must fail safely. |

The complete field shapes, including account-update/login-completion events and
legacy approval methods, are in the fixtures. Do not copy a public example over a
contradicting tested candidate schema.

## Candidate launch and observed restrictions

The probe's immutable override list is in each inventory file and
[probe-codex-profile.py](../../tools/probe-codex-profile.py). It uses `app-server
--listen stdio:// --strict-config`, read-only sandbox, on-request approval,
ChatGPT-only login mode, disabled web search, and false feature overrides for
shell_tool, unified_exec, apps, plugins, hooks, browser_use, computer_use,
multi_agent, js_repl, apply_patch_freeform and shell_snapshot. It sends no tools
and no inference turn.

| Observation | What it establishes / limits |
| --- | --- |
| config/read returns every selected feature override as false | The configuration echo accepted those values; it does not prove effective tool removal. |
| experimentalFeature/list reports unified_exec=true globally and for the loaded thread | Initial inventory was inconclusive. Follow-up model-facing declarations omit execution tools and forced calls are rejected. The mismatch alone is not evidence of usable execution. The original profile still fails inherited-capability scope. |
| Stable thread/start with environments=[] returns -32600 | This restriction requires capabilities.experimentalApi=true for 0.160.1. |
| Experimental thread/start returns environments=[] | Empty environment selection is supported; the schema explicitly says this reports selection, not connection status. It does not establish a filesystem ACL. |
| Seeded enabled MCP server starts; thread-scoped MCP inventory contains one server and one tool without discovery errors | The profile plus environments=[] does not suppress inherited MCP processes or their tool catalogs. A benign helper writes a marker inside its temporary tree, advertises one synthetic tool, and serves inventory only. No tool call is made. |
| Seeded SessionStart hook is absent from hooks/list and its marker is absent | features.hooks=false suppresses this synthetic user-config hook scenario. Managed/plugin hooks and other lifecycle events remain unverified. |
| apps/plugins/browser_use/computer_use flags report false; clean-home plugin inventory empty | No configured plugin/browser/computer integration is exercised. Empty inventory is not proof against inherited or managed capabilities. |
| Other flags such as view_image/image_generation remain true | This is not a verified text-only model-facing tool allowlist. Their actual availability is untested. |
| account/read returns null; public routing absent | The isolated probe has no authenticated account. Shared login, routing identity, refresh, corporate proxies and model access remain untested. |

The temporary home is a **test fixture only**, verified against initialize.codexHome.
Production must reuse the supported existing Codex home/authentication. No user
config/auth file was read, credentials copied, sign-in performed, or actual user
MCP/hook capability launched. The synthetic test home deliberately supplies the
inheritance adversary without involving the user's integrations. Successful probe
exit means inventory completed; `releaseEligible` stays false.

## Subprocess environment contract

Build a fresh child QProcessEnvironment; never inherit the full environment.
Use case-insensitive lookup on Windows and preserve only present values from:

| Group | Allowed names / policy |
| --- | --- |
| Windows execution | SystemRoot, WINDIR, COMSPEC, PATH, PATHEXT |
| User/state directories | USERPROFILE, HOMEDRIVE, HOMEPATH, LOCALAPPDATA, APPDATA, TEMP, TMP |
| Existing Codex state selector | CODEX_HOME, only when already configured; validate it as an existing absolute directory. Do not substitute a new production home or propagate CODEX_SQLITE_HOME independently. |
| Optional proxy/CA integration | HTTP_PROXY, HTTPS_PROXY, ALL_PROXY, NO_PROXY, SSL_CERT_FILE, SSL_CERT_DIR, REQUESTS_CA_BUNDLE, CURL_CA_BUNDLE; validate explicitly. Proxy userinfo is secret and must never be logged. Windows system-proxy behavior needs separate verification. |

Drop all other variables, including OPENAI_API_KEY, provider tokens, ACCESS_TOKEN,
service/database secrets, NODE_OPTIONS, PYTHONPATH and extra Codex state/debug
selectors. PATH and COMSPEC are execution plumbing, not authorization to execute
commands. The probe preserves only the first two groups and supplies its temporary
CODEX_HOME; optional network variables were not tested. Never dump variable values
into logs, support bundles or committed evidence. The allowlist remains provisional
until authenticated refresh and required corporate networking work under it.

## Public account identity binding

For a future verified experimental profile, require account.type == "chatgpt" and
nonempty workspaceRouting.chatgptAccountId/backendOrigin from supported account/read.
Define `codex_account_key` as lowercase hex SHA-256 of UTF-8 compact JSON:
`["devhub-codex-account-v1","chatgpt",backendOrigin,chatgptAccountId]`.
Use the exact server-provided strings, in this order, without whitespace. Preserve
case and content; an origin change invalidates association conservatively. Hashes
are pseudonymous account identifiers, not anonymized data; exclude them from logs.
Never derive ownership from email, token contents, credentials or an advertised model.

Bind this key separately to the canonical Codex home, DevHub account/workspace root
and profile fingerprint. The profile fingerprint will hash the frozen version,
normalized launch overrides, experimental opt-in, capability policy and later tool
registry version. Any missing identity or mismatched scope means New Chat/explicit
reassociation, not automatic cross-restart resume. Stable 0.160.1 has no routing
field, so it cannot meet this automatic restoration contract. Public identity has
only schema evidence here; authenticated availability is an outstanding gate.

## Reproduction and offline checks

Python 3 is developer tooling only; no Python dependency is added to DevHub or its
package. Select the native executable through PATH or an explicit path, and confirm
`--version` is exactly 0.160.1 before collecting this profile. The probe enforces
that version and refuses a different one. A new version requires new fixtures and
review, not overwriting these silently.

```powershell
$candidate = (Get-Command codex.exe -ErrorAction Stop).Source
& $candidate --version
& $candidate app-server --help
& $candidate app-server generate-json-schema --help
& $candidate app-server generate-json-schema --out out/assistant-phase0/schema-stable
& $candidate app-server generate-json-schema --experimental --out out/assistant-phase0/schema-experimental
python tools/freeze-codex-contract.py --schema-root out/assistant-phase0 --output out/assistant-phase0/reproduced
python tools/probe-codex-profile.py --executable $candidate --output out/assistant-phase0/stable.json
python tools/probe-codex-profile.py --executable $candidate --output out/assistant-phase0/experimental.json --experimental
python tools/probe-codex-profile.py --executable $candidate --output out/assistant-phase0/adversarial.json --experimental --seed-capabilities
python tests/codex_contract_test.py
```

The offline suite checks pinned hashes, reference closure, preserved field names,
experimental fields, ID types, routing, streaming identities, terminal enums,
approval/result separation and the rejected-profile evidence. Raw diagnostics are
discarded by the probe; records have a 1 MiB cap, requests have a 15-second deadline,
and the direct child is closed/terminated/killed with bounded waits. This is not
the production transport or a claim of process-tree containment. Inventory counts
can vary across machines; review differences rather than blindly replacing fixtures.

## Remaining release gates and phase handoff

Phase 0's contract/evidence and bounded verification deliverables are complete.
See the follow-up report for the positive/negative controls and exact exclusions;
checklist completion does not approve unavailable profiles or live inference.

Before any live inference, implement the tested admission/disablement contract,
reject incompatible managed/attached-runtime profiles, and validate the actual
ChatGPT/model configuration. The bounded read controls do not claim general
filesystem ACLs, clean-machine compatibility or arbitrary write containment.

Further gates: authenticated ChatGPT reuse/refresh, public identity availability,
model entitlement, streaming/Stop/history restoration, Windows descendant cleanup,
filtered proxy/CA support and packaged clean-machine behavior. Transport/framing
Phase 1 can proceed offline using these fixtures and a fake server. Phase 2 must
repeat the capability gate before enabling inference; successful initialize alone
must never open it.

The [Phase 1 transport report](assistant-transport.md) records the implemented
process/codec/locator boundaries, offline fault tests and native cleanup probe.
The [Phase 2 client report](assistant-client.md) records typed protocol operations,
the implemented runtime admission checks and the continuing native inference gate.

Official references rechecked on the evidence date:
[App Server](https://learn.chatgpt.com/docs/app-server),
[configuration](https://learn.chatgpt.com/docs/config-file/config-reference), and
[environment variables](https://learn.chatgpt.com/docs/config-file/environment-variables).
Runtime observations and wire details above come from the native candidate and its
generated schemas; documentation is supporting context, not a substitute for proof.
