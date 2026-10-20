# Assistant typed client and runtime profile

The current adapter, runtime verification and semantic binding rules supersede
the original version gates below; see [Codex compatibility](codex-compatibility.md).

Phase 2 adds `CodexClient` to the existing Qt Core `devhub_assistant` library.
Phase 3 now wires a lazily started backend and Assistant controller/page in
bootstrap; see [the UI report](assistant-ui.md). Native Send is enabled for the
user-requested 0.162.0-alpha.17.2 after runtime account/profile/model checks.
Protocol operations are verified through an in-memory transport;
no native sign-in, logout, credential copying or real inference was performed.

## Wire contract and request lifetime

The client embeds immutable generated experimental projections for 0.160.1 and
0.162.0-alpha.17.2 through
`resources/codex_contracts.qrc`. Both outgoing operation shapes and incoming
required response/notification fields are checked against that exact snapshot.
The structural validator supports every constraint keyword in the projection,
rejects unsupported keywords and limits recursion to 64 and validation to 100,000
nodes. Formats and defaults are annotations. Semantic policy further restricts
IDs, history, authentication, paths, capabilities and URL destinations.

Client request IDs are monotonically allocated JSON safe-range integers and are
never reset on restart. Each pending operation holds its process generation,
account epoch, expected thread/turn association and monotonic acknowledgement
deadline. The default acknowledgement is five seconds; login-start allows fifteen
seconds. A ten-millisecond timer checks each request's own deadline. Acknowledging
a turn is distinct from completing generation: there is no whole-answer timeout.

Server IDs occupy a separate map keyed by their JSON representation, preserving
integer versus string identity. The map retains only request hashes, with a limit
of 256 distinct routes per process. Exact duplicate server requests receive no
second reply; conflicting reuse closes the connection. Client duplicate, late,
unknown and old-generation responses are ignored. No uncertain turn or thread
creation is automatically replayed; an acknowledgement timeout requires reconnect.
Pending operations fail once on transport loss or stop. A new launch is refused
until the previous owned transport reports stopped.

Initialize uses `clientInfo.name=devhub`, `title=DevHub`, the application version,
and `capabilities.experimentalApi=true`. No extensions or capability roots are
declared. `initialized` is sent once, only after the required initialize result
has passed validation. Duplicate process-start notifications cannot initialize
twice. Initialize then requests public account status and effective configuration;
it does not negotiate or prove tool restrictions.

An explicit initialize rejection now closes the backend instead of leaving the
page in Starting with no pending request. The controller also bounds process
startup and handshake at 20 seconds, stops an unresponsive backend, and retains
the startup failure in the visible status until Reconnect. Reconnect invalidates
the previous executable probe before cancelling it and resets the startup timer;
old-generation responses cannot restore readiness.

## Account and sign-in

Account status distinguishes unknown, signed-out, ChatGPT, API key, other and
expired modes. `requiresOpenaiAuth=false` and an advertised model do not satisfy
the ChatGPT requirement. Email and raw routing fields are discarded. The account
fingerprint follows the Phase 0 SHA-256 contract over the exact public routing
strings. Missing routing identity prevents binding/resuming a conversation.

Account notifications revoke readiness immediately and refresh public status.
An older read cannot restore readiness after an update. Identity changes also
invalidate in-flight thread/turn associations. An account update or changed
identity during an active turn closes the connection and completes that turn as
uncertain; later events cannot append text from the revoked account scope.
Failed account checks leave Send
disabled. A typed terminal `unauthorized` error marks the account expired without
copying its raw message or token data into application errors.

Only managed `chatgpt` browser and `chatgptDeviceCode` requests are exposed. URL,
login ID and device code remain transient challenge values. The client does not
open a browser or implement an OAuth callback. HTTPS URLs must match the admitted
auth host/path, with no userinfo, fragment or nonstandard port: browser challenges
use `/oauth/authorize` on `auth.openai.com` or `chatgpt.com`; device challenges use
`https://auth.openai.com/codex/device`. A different endpoint requires profile review.
These destination checks have fixture evidence, not a completed native sign-in.

Completion matches the current login ID. Bounded early completions are reconciled
against the subsequent challenge. Cancellation clears the UI challenge first and
sends the actual login ID. Late completion cannot revive a cancelled challenge.
A five-minute application lifetime clears/cancels an abandoned challenge; the
pinned response has no server expiry field, so this is not a claim about token
expiry. No API-key/token login or global logout method is available.

Model pages preserve catalog ID, model name, display name, hidden/default state,
text modality, availability notice and cursor. `entitlementVerified` remains false.
No model access is inferred from catalog membership or a ChatGPT plan label.

## Backend restrictions

`CodexRuntimeProfile` constructs canonical argv for the pinned native candidate:
stdio App Server with strict config, disabled shell/unified execution,
apps/plugins/hooks/browser/computer/goals/image generation/multi-agent/JS REPL/
patch/snapshot features, disabled web search, read-only sandbox, on-request
approvals, managed ChatGPT login and the OpenAI provider. Each explicitly selected
inherited MCP server is disabled through its own quoted TOML table key in a
single table override. Quoted dotted CLI paths are rejected by the pinned native
alpha and are not used. Legacy notify and desktop ambient suggestions are disabled for the
child. Dots, quotes and Unicode in names cannot become a different table or
command. Arbitrary overrides or additional launch options are rejected.

The client admits only an explicit OS-plumbing environment allowlist. Secrets and
arbitrary variables are rejected. An optional existing `CODEX_HOME` must be an
absolute directory and must match initialize's canonical home. The client does
not replace the production credential home. Proxy/CA additions need later native
verification and are currently rejected rather than silently inherited.

After initialize, `config/read` requests layers and `configRequirements/read`
requests managed requirements. Until both succeed, thread operations are denied.
Effective restricted features must be explicitly false, every inherited MCP
entry explicitly disabled, legacy notify absent/empty, and provider/login/default
policy compatible. Missing/malformed values, provider overrides, extra roots,
unknown/enterprise/MDM config sources, unimplemented managed requirements and attached runtime
configuration are rejected. Known appearance/queue desktop preferences are
permitted only with ambient suggestions disabled. This supports fewer managed profiles
than the native server. Raw config/layer values exist only transiently during the
check and are discarded; rejection diagnostics contain fixed policy reasons.
When inherited MCP is the sole conflict, the client supplies canonical disable
arguments from the supported RPC. The controller permits one automatic restart,
denies Send throughout it, and rechecks the complete effective profile. Manual
Reconnect returns to the base profile and discovers current server names again.

Local `system` file layers use the same effective restrictions as user/project
layers. A non-null `allowedLoginMethods` requirement is compatible only when its
known method list includes `chatgpt`; DevHub still enforces and independently
verifies ChatGPT authentication. All other non-null requirements remain rejected.

Thread creation uses legacy history, no selected/runtime roots
and `environments=[]`. Every admitted turn repeats empty environments and read-only
policy. Resume has no environments/dynamicTools fields in the frozen contract.
Legacy persisted metadata may omit environment assignments or return null.
Alpha paginated resume may reconstruct exactly one environment in the canonical
scratch directory with no runtime roots; foreign or additional environments are
rejected. Every admitted turn still explicitly clears assignments. Bindings
include the public account fingerprint, canonical home/scratch directory and a
version-scoped fingerprint of the actual canonical launch arguments,
capability/history policy, and configured tool inventory.

When the application-tool runtime is composed, both pinned profiles opt into the tagged
experimental inventory. Tool item notifications/history retain identity only,
excluding arguments and content from transcript state.
Reads/resumes with a different binding are refused.

`CodexRuntimeProfile::productionChatEnabled(version)` enables production native
chat for the user's pinned 0.162.0-alpha.17.2. Other native versions remain gated.
This does not bypass effective profile, account routing, sign-in, pending request,
thread binding, model selection or scope checks. The transport defaults to native;
only the in-memory fake compiled into test targets declares `InMemoryTest` and
exercises synthetic turns after the same account/profile checks. `CodexProcess`
is final and always uses the native default. The Phase 0 behavioral controls
remain the evidence for capability suppression; passing fake tests or initialize
does not certify authenticated native model access or broaden those controls.

Unknown server methods receive `-32601`. Native command/file approvals decline,
legacy approvals abort, and permission requests grant an empty profile. Dynamic
calls reach only the configured scoped application registry; unknown/denied calls
return unsuccessful text results. Phase 9 application mutations use separate
one-use inline approvals and existing Notes/Services/workspace owners. Native
questions, MCP elicitation and external token refresh receive method-not-found
errors. Application approvals never enable native command/file execution.

## Threads, turns and items

Thread handles come from `result.thread.id`, never unrelated `thread/started`
events. Read/resume use the supplied scoped handle and `includeTurns=true` for
legacy hydration. Alpha resume requests `initialTurnsPage` with full items,
ascending order and a limit one above the history cap. Its complete page supplies
hydration; a nonempty `nextCursor`, partial item view, live turn, wrong
thread/root/provider, foreign environment or disabled item type is rejected.
Backwards cursors are page boundaries and do not invalidate an otherwise complete
ascending page. Other paginated responses remain unsupported.
Defaults cap stored history at 100 turns / 8 MiB, and each turn at 128
items with 2 MiB per text item.

Only one turn can be pending/active. Early turn events are buffered up to 64
records / 256 KiB, then matched against the acknowledged thread/turn identity.
Alpha live acknowledgements and turn notifications may use `itemsView=notLoaded`
with an empty items array; streamed item records remain the source of answer text.
The tested alpha also sends successful terminal summaries containing one final
agent message. That item is validated and merged into the retained stream;
earlier commentary/tool identities remain intact. Other partial live views and
unloaded payloads containing items are rejected. Restored history requires full
items; summaries cannot hydrate a saved transcript.
Unrelated events never enter the answer. Multiple agent items retain their IDs
and arrival order. Deltas may precede item-start; completed item text replaces the
streamed value rather than being appended. Repeated final items/terminal events
cannot produce a second completion. Deltas have no frozen sequence identifier,
so identical adjacent delta text is not heuristically deduplicated.

Terminal status distinguishes completed, failed, interrupted and uncertain
connection-loss state. A completed event with `inProgress` is incompatible.
Interrupt sends the actual thread/turn IDs and its acknowledgement does not finish
the turn. Optional unfrozen notifications, including retry-error notifications,
are ignored rather than guessed; terminal error/status comes from the typed turn.

`account/updated` has no account identifier. A ChatGPT update therefore suspends
admission and boundedly queues scoped responses, turn/item traffic, dynamic calls
and outgoing tool results while a fresh `account/read` verifies identity and auth
requirements. An unchanged fingerprint preserves pending/active work, including
token or plan refreshes; superseded account checks do not become chat errors.
Stale replies cannot confirm identity. Actual account changes, sign-out, failed
verification, deadline or queue overflow still revoke the active scope and retain
uncertain delivery without replay. Queued tool results require the original thread
binding after verification and cannot be sent across an account change.

## Verification and handoff

Build and run without Codex, networking, account credentials or a model endpoint:

```powershell
cmake --build out/build/qml-migration --config Debug --parallel
ctest --test-dir out/build/qml-migration -C Debug --output-on-failure
python tests/codex_contract_test.py
```

`devhub.assistant-client` covers all eighteen Phase 2 checklist entries, including
direction/type collisions, malformed required fields, deadlines, late/duplicate
messages, generation loss/restart, account changes, both login challenges and
cancellation/expiry, history scope/caps, early-event races, final reconciliation,
terminal errors and every server-denial form. Additional tests cover callbacks
that start another turn or reconnect, failed writes, request caps and the native
inference gate. Existing transport/framing and application regressions remain in
CTest; CI prints the saved client report when a suite fails.

Local verification on 2026-10-09:

- Full application Debug build/link passed with Qt 6.11.0 and MinGW.
- All ten CTest suites passed, including 75 client, 21 protocol and 25 process
  checks (Qt Test counts include setup/cleanup).
- All ten Python frozen-contract checks passed; Phase 0 snapshots remain unchanged.
- The three new implementation sources and client tests compiled with MSVC 14.44,
  `/O2 /W4 /WX`, against the available Qt 6.11.0 headers. This is a compile check,
  not a linked MSVC/Qt 6.8.3 CI or packaged-runtime result.

Native authentication/refresh, current real model access, attached runtimes,
managed-policy controls, proxy/CA behavior and packaged Qt 6.8.3 runtime validation
remain release gates. Later presentation/session integration must also enforce
DevHub account/workspace lifetime; this client is not the conversation controller.

Wire shapes come from the pinned local schemas. Supporting guidance was checked
against [official App Server documentation](https://learn.chatgpt.com/docs/app-server)
on 2026-10-09; generated version-specific projections remain the wire contract.
