# Assistant Phase 0 capability verification

Evidence date: 2026-10-09. Candidate: native `codex-cli 0.160.1`.

The three remaining Phase 0 checks have now been exercised against an explicitly
bounded profile. Native execution and empty-environment restrictions pass the
scripted controls below. Inherited MCP requires individual server disablement;
plugins and hooks require their feature restrictions. The original candidate
profile remains rejected. This is not approval for authenticated inference or for
unavailable managed/browser/computer integrations.

## Test method and limits

[verify-codex-capabilities.py](../../tools/verify-codex-capabilities.py) launches the
actual native App Server with a disposable Codex home and scratch directory. A
loopback HTTP fixture supplies scripted Responses events and records tool
declarations in memory. There are no real model requests, credentials, API keys,
ChatGPT login or user integrations. The custom provider exists only in the test;
it is not an alternative DevHub inference backend or a billing fallback.

The native harness runs a full turn against those scripted events. Controls read
known text markers inside and outside the scratch directory, and load a generated
one-pixel PNG outside it. All targets stay within the temporary fixture tree.
The enabled read controls use danger-full-access to avoid confusing approval
denial with disabled tools. They pass only known marker reads, with login-shell
semantics disabled. Restricted cases retain read-only sandbox and deny all
server-originated requests.

Hook execution controls use `codex exec` against the same local provider and
synthetic config. The CLI's one-invocation hook-trust bypass is used only there
to make the enabled control execute a known helper. In App Server, that top-level
flag does not make the seeded hook trusted: hooks/list still reports untrusted.
The App Server controls prove hook discovery/removal, while the CLI pair proves
that the helper runs with hooks enabled and does not run with hooks disabled.
Neither is presented as proof of managed-hook behavior.

A real local plugin is installed through native Codex CLI into the disposable
home from a local marketplace. It bundles the benign MCP helper. No plugin is
installed into the user's Codex home. Temporary plugin/config/cache state is
removed after each case. Raw requests, tool arguments, paths, outputs and process
diagnostics are not persisted in the evidence.

## Results

The [saved evidence](../../tests/fixtures/codex/0.160.1/capability-verification.json)
contains 27 scenarios and ten derived checks, all passing.

| Requirement | Control and restricted result | Conclusion |
| --- | --- | --- |
| Disable native shell/unified execution | Enabling shell_tool/unified_exec with a default environment advertises exec_command/write_stdin and reads both markers. Candidate overrides omit these tools; forced exec_command and shell calls return unsupported-call results without marker reads. | Verified for this native candidate and scripted model configuration. The earlier unified_exec inventory discrepancy is not proof that a usable execution tool remains. |
| Empty thread environments | With native execution features deliberately enabled, thread/start environments=[] removes execution tools and rejects forced exec_command. It also removes view_image and rejects a forced outside-root image read. | Verified tool-surface and dispatch restriction. This is not a filesystem ACL or a claim that Codex cannot read its own configuration/authentication state. |
| Empty turn environments | Starting a thread with a default environment and sending turn/start environments=[] removes the same execution/image tools and rejects the calls. A subsequent turn omitting environments retains the empty selection. | Turn override and sticky behavior verified. Stable API rejection remains separately recorded. |
| Inherited MCP | An enabled seeded server starts and advertises its tool even with environments=[]. mcp_servers={} still retains it. Setting mcp_servers.devhub_sentinel.enabled=false prevents startup, removes MCP/resource tools, and rejects a forced call. | Individual disablement is required. Neither empty environments nor an empty table disables inherited MCP. |
| Inherited plugins | The installed enabled plugin starts its bundled server and exposes its tool. features.plugins=false prevents startup and removes its tools despite the installation remaining present. | Verified for a configured local plugin with bundled MCP. Unknown plugin mechanisms remain outside the compatibility profile. |
| User-config hooks | App Server lists the seeded hook when enabled and removes it when disabled. The trusted CLI control writes the marker only when hooks are enabled. | Verified discovery/removal and user-config execution controls; App Server trust and managed-hook cases are explicitly distinguished. |
| Browser/computer capabilities | Neither namespace is offered on this host, including when their feature flags are enabled. The restricted profile keeps both disabled; scripted browser_use/computer_use calls are rejected as unsupported. No browser or native app is opened. | Verified absence and rejection on this host. An attached native browser/computer runtime is unavailable here and is excluded rather than certified. |
| Remaining built-ins | features.goals=false removes create_goal/get_goal/update_goal. Hardened cases expose only request_user_input, which is a client-mediated request and grants no filesystem/process access. | Deny unsupported native questions/approvals in the future client; do not treat their presence as a grant. |

The earlier recorded `config/read` versus `experimentalFeature/list` mismatch is
retained as historical evidence. Model-facing tool declarations and forced calls
now supply the missing behavioral evidence. No real execution-enabled profile is
approved for product use by the positive controls.

## Bounded text-only profile and admission policy

Start with the original feature restrictions, adding `features.goals=false` and
`features.image_generation=false`. Require empty environments on thread creation
and each admitted turn. Keep plugins/apps/hooks/browser/computer use disabled;
do not declare additional client extensions or selected capability roots. Disable
every inherited MCP server by its exact configured name before any thread starts.
The fixed sentinel name in the tests is not a production discovery strategy.

The tested initialize/config-read sequence does not start either seeded MCP
process. The future client can inspect supported effective config in memory,
collect server names, restart with per-server false overrides, and recheck before
thread creation. Serialize server names as quoted TOML key components within
separate QProcess arguments; never interpolate a command string. Discard command,
environment, URL and credential values. Unknown or newly enabled servers invalidate
admission. Never write the user's config to achieve this.

The test tool's `admission_reasons` function is the Phase 0 policy prototype.
Phase 2 now implements the stricter shipped library policy in `CodexRuntimeProfile`
and gates operations through `CodexClient`; see [the client report](assistant-client.md).
The prototype rejects enabled/missing restricted
feature values, inherited enabled MCP servers, enabled legacy notify callbacks,
malformed configuration, managed hooks and conflicting managed feature requirements.
Unsafe controls intentionally continue within their disposable tree to demonstrate
the behavior; production must stop at the rejection. Ten offline tests validate
the fixtures and rejection cases.

Before product inference, require all of these additional conditions:

- Effective config and configRequirements/read are understood and conflict-free.
  Required managed hooks or execution capabilities are rejected; do not bypass
  organizational policy. Unknown managed sources remain unsupported.
- No attached native browser/computer runtime or additional capability roots are
  admitted until that exact integration has its own positive/negative control set.
  Absence on this machine is not a universal disablement claim.
- The actual selected model/catalog and ChatGPT authentication path are tested.
  The loopback fixture uses a pinned catalog entry to exercise the native harness;
  it does not prove real model entitlement, shared authentication or identity.
- The future client enforces empty environment selection, rejects disabled server
  requests, and revalidates configuration/process generations before a turn.

`fixtureScopePassed=true` records the bounded native tests. `releaseEligible=false`
remains in the artifact. The original candidate is still rejected for text-only
scope because it retains inherited MCP and goal tools; the strengthened profile
is a verified development contract with explicit exclusions, not a live release.

## Reproduction

```powershell
$candidate = (Get-Command codex.exe -ErrorAction Stop).Source
python tools/verify-codex-capabilities.py --executable $candidate --output out/assistant-phase0/capability-verification.json
python tests/codex_contract_test.py
```

The verifier enforces 0.160.1, bounds protocol records/requests, disposes its child
and loopback listener, and exits unsuccessfully if any derived check fails. The
offline tests require no Codex installation and perform no networking. Python
remains developer tooling; DevHub's production dependencies/build are unchanged.

Supporting official documentation, rechecked on the evidence date:
[MCP/feature configuration](https://learn.chatgpt.com/docs/config-file/config-reference),
[hook trust and disablement](https://learn.chatgpt.com/docs/hooks), and
[local plugin packaging](https://developers.openai.com/plugins/build/plugins).
The behavioral conclusions above come from the native candidate and fixtures.
