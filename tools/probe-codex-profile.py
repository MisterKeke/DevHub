"""Opt-in, unauthenticated Phase 0 inventory. Never uses the user's Codex home.

This is developer evidence tooling, not DevHub's production transport.
Only selected booleans/counts are persisted; raw RPC and stderr are discarded.
"""

import argparse
import json
import os
from pathlib import Path
import queue
import subprocess
import tempfile
import threading
import time


ENVIRONMENT_NAMES = (
    "SystemRoot", "WINDIR", "COMSPEC", "PATH", "PATHEXT", "USERPROFILE",
    "HOMEDRIVE", "HOMEPATH", "LOCALAPPDATA", "APPDATA", "TEMP", "TMP",
)
OVERRIDES = (
    "features.shell_tool=false", "features.unified_exec=false",
    'web_search="disabled"', 'sandbox_mode="read-only"',
    'approval_policy="on-request"', 'forced_login_method="chatgpt"',
    "features.apps=false", "features.plugins=false", "features.hooks=false",
    "features.browser_use=false", "features.computer_use=false",
    "features.multi_agent=false", "features.js_repl=false",
    "features.apply_patch_freeform=false", "features.shell_snapshot=false",
)


class Probe:
    def __init__(self, executable, cwd, codex_home, overrides=OVERRIDES, extra_args=(), expected_version="0.160.1"):
        allow = {name.casefold() for name in ENVIRONMENT_NAMES}
        env = {k: v for k, v in os.environ.items() if k.casefold() in allow}
        # Test-only state selector. No auth files are read or copied.
        env["CODEX_HOME"] = str(codex_home)
        version = subprocess.run([str(executable), "--version"], env=env, cwd=cwd,
                                 capture_output=True, timeout=5, check=True)
        if version.stdout.strip() != ("codex-cli " + expected_version).encode():
            raise RuntimeError("candidate-version-mismatch")
        args = [str(executable), *extra_args, "app-server", "--listen", "stdio://", "--strict-config"]
        for override in overrides:
            args.extend(["-c", override])
        self.process = subprocess.Popen(args, cwd=cwd, env=env, stdin=subprocess.PIPE,
                                        stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        self.inbox = queue.Queue(maxsize=256)
        self.next_id = 0
        self.notification_counts = {}
        self.turn_events = []
        self.stderr_bytes = 0
        self.failure = None
        self.readers = [threading.Thread(target=self.read_stdout, daemon=True),
                        threading.Thread(target=self.read_stderr, daemon=True)]
        for reader in self.readers:
            reader.start()

    def read_stdout(self):
        try:
            while True:
                line = self.process.stdout.readline(1024 * 1024 + 1)
                if not line:
                    return
                if len(line) > 1024 * 1024 or not line.endswith(b"\n"):
                    self.failure = "record-cap-or-incomplete-record"
                    return
                value = json.loads(line)
                if not isinstance(value, dict):
                    self.failure = "non-object-record"
                    return
                self.inbox.put(value, timeout=1)
        except (ValueError, queue.Full):
            self.failure = "invalid-record-or-inbox-cap"

    def read_stderr(self):
        while chunk := self.process.stderr.read(4096):
            self.stderr_bytes += len(chunk)

    def send(self, value):
        self.process.stdin.write(json.dumps(value, separators=(",", ":")).encode() + b"\n")
        self.process.stdin.flush()

    def notification(self, value):
        # Never approve or execute server-originated requests.
        if "id" in value:
            self.send({"id": value["id"], "error": {"code": -32601,
                       "message": "Disabled by Phase 0 inventory"}})
        method = value["method"]
        self.notification_counts[method] = self.notification_counts.get(method, 0) + 1
        if method == "turn/completed":
            if len(self.turn_events) >= 32:
                raise RuntimeError("turn-event-cap")
            self.turn_events.append(value)

    def wait_turn(self, thread_id, turn_id):
        deadline = time.monotonic() + 15
        while time.monotonic() < deadline:
            for index, value in enumerate(self.turn_events):
                params = value.get("params", {})
                if params.get("threadId") == thread_id and params.get("turn", {}).get("id") == turn_id:
                    return self.turn_events.pop(index)["params"]["turn"]
            if self.failure:
                raise RuntimeError(self.failure)
            try:
                value = self.inbox.get(timeout=0.1)
            except queue.Empty:
                if self.process.poll() is not None:
                    raise RuntimeError("app-server-exited")
                continue
            if "method" in value:
                self.notification(value)
        raise RuntimeError("turn-deadline")

    def request(self, method, params):
        self.next_id += 1
        request_id = self.next_id
        self.send({"id": request_id, "method": method, "params": params})
        deadline = time.monotonic() + 15
        while time.monotonic() < deadline:
            if self.failure:
                raise RuntimeError(self.failure)
            try:
                value = self.inbox.get(timeout=0.1)
            except queue.Empty:
                if self.process.poll() is not None:
                    raise RuntimeError("app-server-exited")
                continue
            if "method" in value:
                self.notification(value)
                continue
            if value.get("id") == request_id:
                return value
        raise RuntimeError("request-deadline")

    def close(self):
        self.process.stdin.close()
        try:
            self.process.wait(timeout=3)
        except subprocess.TimeoutExpired:
            self.process.terminate()
            try:
                self.process.wait(timeout=3)
            except subprocess.TimeoutExpired:
                self.process.kill()
                self.process.wait(timeout=3)
        for reader in self.readers:
            reader.join(timeout=1)
        self.process.stdout.close()
        self.process.stderr.close()


def summarize(method, response):
    if "error" in response:
        return {"ok": False, "errorCode": response["error"].get("code")}
    result = response.get("result", {})
    summary = {"ok": True}
    if method == "initialize":
        summary["requiredFieldsPresent"] = all(k in result for k in (
            "userAgent", "codexHome", "platformFamily", "platformOs"))
    elif method == "config/read":
        config = result.get("config", {})
        features = config.get("features", {})
        summary["disabledFeatures"] = {k: features.get(k) is False for k in (
            "shell_tool", "unified_exec", "apps", "plugins", "hooks", "browser_use",
            "computer_use", "multi_agent", "js_repl", "apply_patch_freeform", "shell_snapshot")}
        summary["webSearchDisabled"] = config.get("web_search") == "disabled"
        summary["readOnlySandbox"] = config.get("sandbox_mode") == "read-only"
        summary["chatgptOnly"] = config.get("forced_login_method") == "chatgpt"
        summary["configKeys"] = sorted(config)
    elif method == "experimentalFeature/list":
        summary["features"] = {f["name"]: f["enabled"] for f in result.get("data", [])
                               if isinstance(f.get("enabled"), bool)}
        summary["hasMore"] = result.get("nextCursor") is not None
    elif method == "hooks/list":
        entries = result.get("data", [])
        summary["hookCount"] = sum(len(e.get("hooks", [])) for e in entries)
        summary["enabledHookCount"] = sum(h.get("enabled") is True
                                         for e in entries for h in e.get("hooks", []))
        summary["errorCount"] = sum(len(e.get("errors", [])) for e in entries)
        summary["trustStatuses"] = sorted({h.get("trustStatus", "unknown")
                                            for e in entries for h in e.get("hooks", [])})
    elif method == "mcpServerStatus/list":
        entries = result.get("data", [])
        summary["serverCount"] = len(entries)
        summary["toolCount"] = sum(len(e.get("tools", {})) for e in entries)
        summary["toolsErrorCount"] = sum(e.get("toolsError") is not None for e in entries)
        summary["hasMore"] = result.get("nextCursor") is not None
    elif method == "account/read":
        summary["accountNull"] = result.get("account") is None
        summary["routingPresent"] = result.get("workspaceRouting") is not None
    elif method == "thread/start":
        thread = result.get("thread", {})
        summary["threadIdPresent"] = isinstance(thread.get("id"), str)
        environments = thread.get("environments")
        summary["environmentCount"] = len(environments) if isinstance(environments, list) else None
        summary["environmentFieldPresent"] = "environments" in thread
    elif method == "configRequirements/read":
        summary["requirementsPresent"] = result.get("requirements") is not None
    else:
        # Inventory structure only, no names, locations or hook commands.
        summary["resultKeys"] = sorted(result)
        summary["listCounts"] = {k: len(v) for k, v in result.items() if isinstance(v, list)}
        summary["hasMore"] = result.get("nextCursor") is not None
    return summary


def seed_capabilities(codex_home, root):
    """Harmless synthetic MCP server/hook, confined to the temporary probe tree."""
    helper = root / "capability-sentinel.py"
    helper.write_text('''import json, pathlib, sys
marker = pathlib.Path(sys.argv[1])
marker.write_text("started", encoding="utf-8")
if sys.argv[2] == "hook":
    raise SystemExit(0)
for line in sys.stdin:
    request = json.loads(line)
    if "id" not in request:
        continue
    method = request.get("method")
    if method == "initialize":
        result = {"protocolVersion": "2024-11-05", "capabilities": {"tools": {}},
                  "serverInfo": {"name": "devhub-sentinel", "version": "1"}}
    elif method == "tools/list":
        result = {"tools": [{"name": "sentinel", "description": "Synthetic test marker",
                            "inputSchema": {"type": "object", "properties": {}}}]}
    else:
        result = {}
    print(json.dumps({"jsonrpc": "2.0", "id": request["id"], "result": result}), flush=True)
''', encoding="utf-8")
    import sys
    hook_command = subprocess.list2cmdline([
        sys.executable, str(helper), str(root / "hook-started"), "hook"])
    config = '\n'.join([
        '[features]', 'hooks = true', 'shell_tool = true', 'unified_exec = true',
        '[mcp_servers.devhub_sentinel]', 'enabled = true',
        'command = ' + json.dumps(sys.executable),
        'args = ' + json.dumps([str(helper), str(root / "mcp-started"), "mcp"]),
        '[[hooks.SessionStart]]', '[[hooks.SessionStart.hooks]]', 'type = "command"',
        'command = ' + json.dumps(hook_command), 'timeout = 5', '',
    ])
    (codex_home / "config.toml").write_text(config, encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--experimental", action="store_true")
    parser.add_argument("--seed-capabilities", action="store_true")
    args = parser.parse_args()
    executable = args.executable.resolve(strict=True)
    if not executable.is_file() or executable.suffix.lower() != ".exe":
        parser.error("Select a native executable")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    evidence = {"profile": "text-only-candidate-0.160.1", "isolatedHome": True,
                "experimentalApi": args.experimental, "overrides": list(OVERRIDES),
                "seededCapabilities": args.seed_capabilities,
                "inferencePerformed": False, "releaseEligible": False, "requests": {}}
    with tempfile.TemporaryDirectory(prefix="probe-", dir=args.output.parent) as root:
        root = Path(root).resolve()
        cwd = Path(root, "scratch")
        codex_home = Path(root, "codex-home")
        cwd.mkdir()
        codex_home.mkdir()
        if args.seed_capabilities:
            seed_capabilities(codex_home, Path(root).resolve())
        probe = Probe(executable, cwd, codex_home)
        try:
            calls = [("initialize", {"clientInfo": {"name": "devhub_phase0_probe",
                       "version": "0.1.0"}, "capabilities": {"experimentalApi": args.experimental}}),
                     ("config/read", {"includeLayers": False}),
                     ("configRequirements/read", None),
                     ("experimentalFeature/list", {"limit": 200}),
                     ("hooks/list", {"cwds": [str(cwd)]}),
                     ("mcpServerStatus/list", {"limit": 100}),
                     ("plugin/list", {"cwds": [str(cwd)], "forceRefetch": False,
                                      "marketplaceKinds": ["local"]}),
                     ("account/read", {"refreshToken": False}),
                     ("thread/start", {"cwd": str(cwd), "ephemeral": True,
                        "sandbox": "read-only", "approvalPolicy": "on-request",
                        "environments": []})]
            for method, params in calls:
                response = probe.request(method, params)
                evidence["requests"][method] = summarize(method, response)
                if method == "initialize":
                    if "error" in response:
                        raise RuntimeError("initialize-rejected")
                    home_matches = Path(response["result"]["codexHome"]).resolve() == codex_home
                    evidence["requests"][method]["isolatedHomeConfirmed"] = home_matches
                    if not home_matches:
                        raise RuntimeError("home-selector-mismatch")
                    probe.send({"method": "initialized"})
                if method == "thread/start" and "result" in response:
                    thread_id = response["result"]["thread"]["id"]
                    for inventory_method, inventory_params in (
                        ("experimentalFeature/list", {"limit": 200, "threadId": thread_id}),
                        ("mcpServerStatus/list", {"limit": 100, "threadId": thread_id}),
                    ):
                        evidence["requests"][inventory_method + "@thread"] = summarize(
                            inventory_method, probe.request(inventory_method, inventory_params))
        except (RuntimeError, OSError) as error:
            # Fixed local errors only; never persist subprocess messages.
            evidence["probeFailure"] = str(error) if isinstance(error, RuntimeError) else "io-error"
        finally:
            probe.close()
            evidence["stderrBytesDiscarded"] = probe.stderr_bytes
            evidence["notificationCounts"] = probe.notification_counts
            evidence["exitCode"] = probe.process.returncode
            if args.seed_capabilities:
                evidence["sentinels"] = {"mcpStarted": Path(root, "mcp-started").exists(),
                                          "hookStarted": Path(root, "hook-started").exists()}
    args.output.write_text(json.dumps(evidence, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps({"output": str(args.output), "probeFailure": evidence.get("probeFailure"),
                      "requests": len(evidence["requests"]), "releaseEligible": False,
                      "sentinels": evidence.get("sentinels")}, sort_keys=True))
    return int("probeFailure" in evidence)


if __name__ == "__main__":
    raise SystemExit(main())
