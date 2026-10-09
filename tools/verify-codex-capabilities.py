"""Opt-in native Codex capability tests against a loopback-only scripted model.

No real inference, auth, API keys, user config or user integrations are used.
Only synthetic paths are passed to tools. Raw model/protocol bodies stay in memory.
"""

import argparse
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import importlib.util
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
import struct
import zlib


spec = importlib.util.spec_from_file_location("profile_probe", Path(__file__).with_name("probe-codex-profile.py"))
profile_probe = importlib.util.module_from_spec(spec)
spec.loader.exec_module(profile_probe)


def admission_reasons(config, requirements):
    """Phase 0 policy prototype. Fail closed before thread creation on conflicts."""
    reasons = []
    if not isinstance(config, dict) or not isinstance(config.get("features"), dict):
        return ["invalid-config-or-features"]
    for name in ("shell_tool", "unified_exec", "apps", "plugins", "hooks", "browser_use",
                 "computer_use", "goals", "image_generation"):
        if config.get("features", {}).get(name) is not False:
            reasons.append("feature-not-disabled:" + name)
    if config.get("web_search") != "disabled":
        reasons.append("web-search-not-disabled")
    if config.get("notify"):
        reasons.append("legacy-notify-enabled")
    if not isinstance(config.get("mcp_servers", {}), dict):
        reasons.append("invalid-mcp-config")
    elif any(not isinstance(server, dict) or server.get("enabled", True) is not False
             for server in config.get("mcp_servers", {}).values()):
        reasons.append("inherited-mcp-enabled")
    if not isinstance(requirements, (dict, type(None))):
        reasons.append("unknown-managed-requirements")
        return reasons
    requirements = requirements or {}
    if requirements.get("hooks"):
        reasons.append("managed-hooks-not-verified")
    feature_requirements = requirements.get("featureRequirements") or {}
    if not isinstance(feature_requirements, dict):
        reasons.append("unknown-managed-feature-requirements")
        return reasons
    for name, value in feature_requirements.items():
        if name in ("shell_tool", "apps", "plugins", "hooks", "browser_use", "computer_use", "goals") and value:
            reasons.append("managed-feature-conflict:" + name)
    return reasons


def evaluate_cases(cases):
    index = {case["case"]: case for case in cases}
    negative = ("native-read-disabled", "shell-read-disabled", "empty-thread-blocks-native-exec",
                "empty-turn-blocks-native-exec", "empty-thread-blocks-image-read", "empty-turn-blocks-image-read",
                "hardened-native-injection", "hardened-mcp-injection", "hardened-browser-injection", "hardened-computer-injection")
    return {
        "allCasesCompleted": not any("failure" in case for case in cases),
        "nativePositiveControlReadBothMarkers": all(index["native-read-enabled-control"].get(key) is True
                                                    for key in ("insideMarkerRead", "outsideMarkerRead")),
        "imagePositiveControlReadImage": index["image-read-enabled-control"].get("imageOutputPresent") is True,
        "disabledCallsRejectedWithoutReads": all(index[name].get("disabledToolRejected") is True
            and not any(index[name].get(key) for key in ("insideMarkerRead", "outsideMarkerRead", "imageOutputPresent"))
            for name in negative),
        "mcpPositiveAndNegativeControl": index["hooks-disabled-control"]["sentinels"]["mcpStarted"]
            and not index["explicit-mcp-disabled"]["sentinels"]["mcpStarted"],
        "emptyMcpTableDoesNotDisableInheritance": index["empty-mcp-table"]["sentinels"]["mcpStarted"],
        "pluginPositiveAndNegativeControl": index["plugin-enabled-control"]["sentinels"]["pluginMcpStarted"]
            and not index["plugin-disabled"]["sentinels"]["pluginMcpStarted"],
        "hookCliPositiveAndNegativeControl": index["cli-hook-enabled-control"].get("hookStarted") is True
            and index["cli-hook-disabled-control"].get("hookStarted") is False,
        "emptyEnvironmentPolicyPersists": index["empty-environments-sticky-followup"].get("followupTools") == ["request_user_input"],
        "hardenedModelSurfaceHasNoIoTools": all(index[name].get("offeredTools") == ["request_user_input"]
            for name in ("explicit-mcp-disabled", "plugin-disabled", "hardened-native-injection",
                         "hardened-mcp-injection", "hardened-browser-injection", "hardened-computer-injection")),
    }


class ScriptedModel:
    def __init__(self, call=None):
        self.call = call
        self.requests = []
        model = self

        class Handler(BaseHTTPRequestHandler):
            def log_message(self, *_):
                pass

            def do_POST(self):
                length = int(self.headers.get("Content-Length", 0))
                if length > 2 * 1024 * 1024:
                    self.send_error(413)
                    return
                raw = self.rfile.read(length)
                body = json.loads(raw)
                if len(model.requests) >= 8:
                    self.send_error(429)
                    return
                model.requests.append(body)
                response_id = "phase0_response_" + str(len(model.requests))
                if model.call and len(model.requests) == 1:
                    name, arguments = model.call
                    item = {"type": "function_call", "id": "phase0_function_1",
                            "call_id": "phase0_call_1", "name": name,
                            "arguments": json.dumps(arguments), "status": "completed"}
                else:
                    item = {"type": "message", "id": "phase0_message_1", "role": "assistant",
                            "status": "completed", "content": [{"type": "output_text",
                            "text": "Synthetic capability test complete.", "annotations": []}]}
                response = {"id": response_id, "object": "response", "model": "gpt-5.4",
                            "status": "completed", "output": [item],
                            "usage": {"input_tokens": 1, "output_tokens": 1, "total_tokens": 2}}
                events = [
                    {"type": "response.created", "response": {**response, "status": "in_progress", "output": []}},
                    {"type": "response.output_item.added", "output_index": 0, "item": item},
                    {"type": "response.output_item.done", "output_index": 0, "item": item},
                    {"type": "response.completed", "response": response},
                ]
                data = b"".join(b"data: " + json.dumps(event).encode() + b"\n\n" for event in events)
                self.send_response(200)
                self.send_header("Content-Type", "text/event-stream")
                self.send_header("Content-Length", str(len(data)))
                self.end_headers()
                self.wfile.write(data)

        self.server = ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()

    def overrides(self):
        return (
            'model="gpt-5.4"', 'model_provider="phase0_fixture"',
            'model_providers.phase0_fixture.name="Phase 0 loopback fixture"',
            'model_providers.phase0_fixture.base_url="http://127.0.0.1:' + str(self.server.server_port) + '/v1"',
            'model_providers.phase0_fixture.wire_api="responses"',
            'model_providers.phase0_fixture.requires_openai_auth=false',
            'model_providers.phase0_fixture.request_max_retries=0',
            'model_providers.phase0_fixture.stream_max_retries=0',
            'model_providers.phase0_fixture.stream_idle_timeout_ms=5000',
            'features.responses_websockets=false', 'features.responses_websockets_v2=false',
            'features.remote_models=false', 'features.image_generation=false',
        )

    def close(self):
        self.server.shutdown()
        self.server.server_close()
        self.thread.join(timeout=2)


def tools_in(body):
    names = []
    for tool in body.get("tools", []):
        kind = tool.get("type")
        if kind == "function":
            names.append(tool.get("name", tool.get("function", {}).get("name", "unknown")))
        elif kind == "namespace":
            names.extend(tool["name"] + "." + child.get("name", "unknown") for child in tool.get("tools", []))
        else:
            names.append(kind)
    return sorted(names)


def marker_png():
    def chunk(kind, payload):
        return struct.pack(">I", len(payload)) + kind + payload + struct.pack(">I", zlib.crc32(kind + payload))
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 1, 1, 8, 2, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(b"\0\xff\0\0")) + chunk(b"IEND", b""))


def install_synthetic_plugin(executable, codex_home, root):
    """Only installs into the disposable Codex home; no remote source or hooks run."""
    marketplace = root / "marketplace"
    manifest_dir = marketplace / ".agents" / "plugins"
    plugin = marketplace / "plugins" / "devhub-plugin-sentinel"
    (plugin / ".codex-plugin").mkdir(parents=True)
    manifest_dir.mkdir(parents=True)
    (manifest_dir / "marketplace.json").write_text(json.dumps({
        "name": "devhub-fixture", "plugins": [{"name": "devhub-plugin-sentinel",
        "source": {"source": "local", "path": "./plugins/devhub-plugin-sentinel"},
        "policy": {"installation": "AVAILABLE", "authentication": "ON_USE"}, "category": "Productivity"}]
    }), encoding="utf-8")
    (plugin / ".codex-plugin" / "plugin.json").write_text(json.dumps({
        "name": "devhub-plugin-sentinel", "version": "1.0.0", "description": "Synthetic capability test",
        "mcpServers": "./.mcp.json"}), encoding="utf-8")
    (plugin / ".mcp.json").write_text(json.dumps({"mcpServers": {"plugin_sentinel": {
        "command": sys.executable, "args": [str(root / "capability-sentinel.py"),
        str(root / "plugin-mcp-started"), "mcp"]}}}), encoding="utf-8")
    import os
    allowed = {key.casefold() for key in profile_probe.ENVIRONMENT_NAMES}
    env = {key: value for key, value in os.environ.items() if key.casefold() in allowed}
    env["CODEX_HOME"] = str(codex_home)
    for operation in (["plugin", "marketplace", "add", str(marketplace), "--json"],
                      ["plugin", "add", "devhub-plugin-sentinel@devhub-fixture", "--json"]):
        result = subprocess.run([str(executable), *operation, "-c", "features.remote_plugin=false"],
            env=env, cwd=root, capture_output=True, timeout=15)
        if result.returncode:
            raise RuntimeError("synthetic-plugin-install-failed")


def run_case(executable, output_parent, name, overrides, environments, call=None, seed=False,
             extra_args=(), turn_environments="inherit", cli_hook_control=False,
             control_sandbox=False, plugin_seed=False, followup=False):
    evidence = {"case": name, "realInference": False, "experimentalApi": True}
    with tempfile.TemporaryDirectory(prefix="capability-", dir=output_parent) as temporary:
        root = Path(temporary).resolve()
        cwd, codex_home = root / "scratch", root / "codex-home"
        cwd.mkdir()
        codex_home.mkdir()
        (cwd / "inside-marker.txt").write_text("DEVHUB_INSIDE_SYNTHETIC_MARKER", encoding="utf-8")
        (root / "outside-marker.txt").write_text("DEVHUB_OUTSIDE_SYNTHETIC_MARKER", encoding="utf-8")
        (root / "outside-marker.png").write_bytes(marker_png())
        if call == "exec-read-markers":
            call = ("exec_command", {"cmd": "Get-Content -LiteralPath '" + str(cwd / "inside-marker.txt")
                + "','" + str(root / "outside-marker.txt") + "'", "max_output_tokens": 100, "login": False})
        elif call == "shell-read-markers":
            call = ("shell", {"command": ["powershell.exe", "-NoProfile", "-Command",
                "Get-Content -LiteralPath '" + str(cwd / "inside-marker.txt") + "','"
                + str(root / "outside-marker.txt") + "'"], "timeout_ms": 5000})
        elif call == "view-image":
            call = ("view_image", {"path": str(root / "outside-marker.png")})
        if seed or plugin_seed:
            profile_probe.seed_capabilities(codex_home, root)
        if plugin_seed:
            install_synthetic_plugin(executable, codex_home, root)
            evidence["syntheticPluginInstalled"] = True
        model = ScriptedModel(call)
        if cli_hook_control:
            allowed = {key.casefold() for key in profile_probe.ENVIRONMENT_NAMES}
            import os
            env = {key: value for key, value in os.environ.items() if key.casefold() in allowed}
            env["CODEX_HOME"] = str(codex_home)
            arguments = [str(executable), "--dangerously-bypass-hook-trust", "exec", "--skip-git-repo-check"]
            for override in overrides + model.overrides():
                arguments.extend(["-c", override])
            arguments.append("Synthetic hook control.")
            try:
                result = subprocess.run(arguments, cwd=cwd, env=env, capture_output=True, timeout=15)
                evidence["exitCode"] = result.returncode
                evidence["hookStarted"] = (root / "hook-started").exists()
                evidence["modelRequestCount"] = len(model.requests)
            finally:
                model.close()
            return evidence
        probe = profile_probe.Probe(executable, cwd, codex_home,
                                    overrides=overrides + model.overrides(), extra_args=extra_args)
        try:
            initialized = probe.request("initialize", {"clientInfo": {"name": "devhub_phase0_capabilities",
                                        "version": "0.1.0"}, "capabilities": {"experimentalApi": True}})
            if "error" in initialized or Path(initialized["result"]["codexHome"]).resolve() != codex_home:
                raise RuntimeError("isolated-initialize-failed")
            probe.send({"method": "initialized"})
            configuration = probe.request("config/read", {"includeLayers": False})
            evidence["config"] = profile_probe.summarize("config/read", configuration)
            requirements = probe.request("configRequirements/read", None)
            if "result" in configuration and "result" in requirements:
                evidence["configurationAdmissionReasons"] = admission_reasons(
                    configuration["result"]["config"], requirements["result"].get("requirements"))
            evidence["preThreadSentinels"] = {"hookStarted": (root / "hook-started").exists(),
                                             "mcpStarted": (root / "mcp-started").exists(),
                                             "pluginMcpStarted": (root / "plugin-mcp-started").exists()}
            params = {"cwd": str(cwd), "ephemeral": True, "sandbox": "read-only",
                      "approvalPolicy": "on-request"}
            if control_sandbox:
                # Only the scripted fixture's known marker reads use this control.
                params["sandbox"] = "danger-full-access"
            if environments is not None:
                params["environments"] = environments
            started = probe.request("thread/start", params)
            if "error" in started:
                evidence["threadErrorCode"] = started["error"]["code"]
                return evidence
            thread_id = started["result"]["thread"]["id"]
            evidence["environmentCount"] = len(started["result"]["thread"].get("environments") or [])
            hooks = probe.request("hooks/list", {"cwds": [str(cwd)]})
            evidence["hooks"] = profile_probe.summarize("hooks/list", hooks)
            turn_params = {"threadId": thread_id,
                "input": [{"type": "text", "text": "Synthetic capability inventory.", "text_elements": []}]}
            if turn_environments != "inherit":
                turn_params["environments"] = turn_environments
            turned = probe.request("turn/start", turn_params)
            if "error" in turned:
                evidence["turnErrorCode"] = turned["error"]["code"]
            else:
                turn = probe.wait_turn(thread_id, turned["result"]["turn"]["id"])
                evidence["turnStatus"] = turn["status"]
                if followup:
                    # Omit the turn override; verify the thread/turn sticky policy survives.
                    followup_result = probe.request("turn/start", {"threadId": thread_id,
                        "input": [{"type": "text", "text": "Synthetic follow-up.", "text_elements": []}]})
                    followup_turn = probe.wait_turn(thread_id, followup_result["result"]["turn"]["id"])
                    evidence["followupStatus"] = followup_turn["status"]
                    evidence["followupTools"] = tools_in(model.requests[-1])
            evidence["modelRequestCount"] = len(model.requests)
            evidence["offeredTools"] = tools_in(model.requests[0]) if model.requests else None
            evidence["hookEventCounts"] = {k: v for k, v in probe.notification_counts.items()
                                            if k.startswith("hook/")}
            if call and len(model.requests) > 1:
                evidence["toolOutputPresent"] = any(item.get("type") == "function_call_output"
                    for item in model.requests[-1].get("input", []))
                outputs = [item for item in model.requests[-1].get("input", [])
                           if item.get("type") == "function_call_output"]
                output_text = json.dumps(outputs)
                evidence["insideMarkerRead"] = "DEVHUB_INSIDE_SYNTHETIC_MARKER" in output_text
                evidence["outsideMarkerRead"] = "DEVHUB_OUTSIDE_SYNTHETIC_MARKER" in output_text
                evidence["unknownToolRejected"] = "unknown tool" in output_text.lower() or "unsupported tool" in output_text.lower()
                evidence["disabledToolRejected"] = any(phrase in output_text.lower() for phrase in (
                    "not available", "disabled", "not enabled", "no environment", "no active environment",
                    "unrecognized function", "unknown function", "unsupported tool", "unknown tool", "unsupported call"))
                evidence["imageOutputPresent"] = '"input_image"' in output_text
        except (RuntimeError, OSError) as error:
            evidence["failure"] = str(error) if isinstance(error, RuntimeError) else "io-error"
        finally:
            probe.close()
            model.close()
            evidence["sentinels"] = {"hookStarted": (root / "hook-started").exists(),
                                      "mcpStarted": (root / "mcp-started").exists(),
                                      "pluginMcpStarted": (root / "plugin-mcp-started").exists()}
    return evidence


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    executable = args.executable.resolve(strict=True)
    cases = []
    for name, overrides, environments, seed, extra_args in (
        ("candidate-empty-environments", profile_probe.OVERRIDES, [], False, ()),
        ("candidate-default-environments", profile_probe.OVERRIDES, None, False, ()),
        ("native-enabled-control", profile_probe.OVERRIDES + (
            "features.shell_tool=true", "features.unified_exec=true"), None, False, ()),
        ("hooks-enabled-control", profile_probe.OVERRIDES + (
            "features.hooks=true",), [], True, ("--dangerously-bypass-hook-trust",)),
        ("hooks-enabled-default-environment", profile_probe.OVERRIDES + (
            "features.hooks=true",), None, True, ("--dangerously-bypass-hook-trust",)),
        ("hooks-disabled-control", profile_probe.OVERRIDES, [], True,
            ("--dangerously-bypass-hook-trust",)),
        ("explicit-mcp-disabled", profile_probe.OVERRIDES + (
            "mcp_servers.devhub_sentinel.enabled=false", "features.goals=false"), [], True, ()),
        ("empty-mcp-table", profile_probe.OVERRIDES + (
            "mcp_servers={}", "features.goals=false"), [], True, ()),
    ):
        case = run_case(executable, args.output.parent, name, overrides, environments,
                        seed=seed, extra_args=extra_args)
        cases.append(case)
        print(json.dumps(case, sort_keys=True), flush=True)
    for name, overrides, environments, call, turn_environments in (
        ("native-read-enabled-control", profile_probe.OVERRIDES + (
            "features.shell_tool=true", "features.unified_exec=true"), None, "exec-read-markers", "inherit"),
        ("native-read-disabled", profile_probe.OVERRIDES, None, "exec-read-markers", "inherit"),
        ("shell-read-disabled", profile_probe.OVERRIDES, [], "shell-read-markers", "inherit"),
        ("empty-thread-blocks-native-exec", profile_probe.OVERRIDES + (
            "features.shell_tool=true", "features.unified_exec=true"), [], "exec-read-markers", "inherit"),
        ("empty-turn-blocks-native-exec", profile_probe.OVERRIDES + (
            "features.shell_tool=true", "features.unified_exec=true"), None, "exec-read-markers", []),
        ("image-read-enabled-control", profile_probe.OVERRIDES, None, "view-image", "inherit"),
        ("empty-thread-blocks-image-read", profile_probe.OVERRIDES, [], "view-image", "inherit"),
        ("empty-turn-blocks-image-read", profile_probe.OVERRIDES, None, "view-image", []),
    ):
        case = run_case(executable, args.output.parent, name, overrides, environments,
                        call=call, turn_environments=turn_environments,
                        control_sandbox=name in ("native-read-enabled-control", "image-read-enabled-control"))
        cases.append(case)
        print(json.dumps(case, sort_keys=True), flush=True)
    for name, enabled in (("cli-hook-enabled-control", True), ("cli-hook-disabled-control", False)):
        case = run_case(executable, args.output.parent, name,
            profile_probe.OVERRIDES + ("features.hooks=" + str(enabled).lower(),), None,
            seed=True, cli_hook_control=True)
        cases.append(case)
        print(json.dumps(case, sort_keys=True), flush=True)
    for name, enabled in (("plugin-enabled-control", True), ("plugin-disabled", False)):
        case = run_case(executable, args.output.parent, name,
            profile_probe.OVERRIDES + ("mcp_servers.devhub_sentinel.enabled=false",
                "features.plugins=" + str(enabled).lower(), "features.goals=false"), [], plugin_seed=True)
        cases.append(case)
        print(json.dumps(case, sort_keys=True), flush=True)
    for name, overrides, environments in (
        ("browser-computer-enabled-default", profile_probe.OVERRIDES + (
            "features.browser_use=true", "features.computer_use=true", "features.goals=false"), None),
        ("browser-computer-enabled-empty", profile_probe.OVERRIDES + (
            "features.browser_use=true", "features.computer_use=true", "features.goals=false"), []),
    ):
        case = run_case(executable, args.output.parent, name, overrides, environments)
        cases.append(case)
        print(json.dumps(case, sort_keys=True), flush=True)
    hardened = profile_probe.OVERRIDES + (
        "mcp_servers.devhub_sentinel.enabled=false", "features.goals=false", "features.image_generation=false")
    for name, call in (
        ("hardened-native-injection", "exec-read-markers"),
        ("hardened-mcp-injection", ("mcp__devhub_sentinel.sentinel", {})),
        ("hardened-browser-injection", ("browser_use", {"url": "http://127.0.0.1"})),
        ("hardened-computer-injection", ("computer_use", {"action": "invalid_phase0_action"})),
    ):
        case = run_case(executable, args.output.parent, name, hardened, [], call=call, seed=True)
        cases.append(case)
        print(json.dumps(case, sort_keys=True), flush=True)
    case = run_case(executable, args.output.parent, "empty-environments-sticky-followup",
                    hardened, None, turn_environments=[], followup=True, seed=True)
    cases.append(case)
    print(json.dumps(case, sort_keys=True), flush=True)
    checks = evaluate_cases(cases)
    evidence = {"format": 1, "codexVersion": "0.160.1", "realInference": False, "cases": cases,
                "checks": checks, "fixtureScopePassed": all(checks.values()), "releaseEligible": False,
                "excludedProfiles": ["managed-hooks-or-conflicting-requirements", "attached-browser-or-computer-runtime",
                                     "unverified-model-catalog", "authenticated-live-chatgpt"]}
    args.output.write_text(json.dumps(evidence, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps({"cases": len(cases), "checks": checks, "releaseEligible": False}, sort_keys=True), flush=True)
    return int(not all(checks.values()))


if __name__ == "__main__":
    raise SystemExit(main())
