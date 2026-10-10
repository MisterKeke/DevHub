"""Opt-in native dynamic-tool compatibility check with a loopback scripted model.

No authentication or real inference. Uses an empty disposable CODEX_HOME and
synthetic tool output. This is protocol evidence, not live release approval.
"""
import argparse
import importlib.util
import json
from pathlib import Path
import queue
import tempfile


def module(name, filename):
    spec = importlib.util.spec_from_file_location(name, Path(__file__).with_name(filename))
    loaded = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(loaded)
    return loaded


capabilities = module("capability_checks", "verify-codex-capabilities.py")
profile = capabilities.profile_probe


class ToolProbe(profile.Probe):
    def __init__(self, *args, **kwargs):
        self.tool_calls = 0
        self.valid_call = False
        super().__init__(*args, **kwargs)

    def notification(self, value):
        if value.get("method") == "item/tool/call" and "id" in value:
            params = value.get("params", {})
            self.tool_calls += 1
            self.valid_call = (params.get("tool") == "devhub_git_status"
                               and params.get("arguments") == {}
                               and bool(params.get("threadId")) and bool(params.get("turnId"))
                               and bool(params.get("callId")))
            self.send({"id": value["id"], "result": {"success": self.valid_call,
                "contentItems": [{"type": "inputText", "text": json.dumps({
                    "success": self.valid_call, "data": {"isRepository": True}})}]}})
        else:
            super().notification(value)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--executable", type=Path, required=True)
    parser.add_argument("--version", choices=("0.160.1", "0.162.0-alpha.17.2"), required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    executable = args.executable.resolve(strict=True)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    checks = {}
    with tempfile.TemporaryDirectory(prefix="dynamic-tools-", dir=args.output.parent) as temporary:
        root = Path(temporary).resolve()
        cwd, home = root / "scratch", root / "codex-home"
        cwd.mkdir(); home.mkdir()
        model = capabilities.ScriptedModel(("devhub_git_status", {}))
        probe = None
        try:
            overrides = profile.OVERRIDES + ("features.goals=false", "features.image_generation=false") + model.overrides()
            probe = ToolProbe(executable, cwd, home, overrides=overrides, expected_version=args.version)
            result = probe.request("initialize", {"clientInfo": {"name": "devhub_offline_read_tools", "version": "1"},
                "capabilities": {"experimentalApi": True}})
            checks["isolatedHome"] = Path(result["result"]["codexHome"]).resolve() == home
            probe.send({"method": "initialized"})
            spec = {"type": "function", "name": "devhub_git_status",
                "description": "Read Git status in the selected workspace.",
                "inputSchema": {"type": "object", "properties": {}, "required": [], "additionalProperties": False}}
            result = probe.request("thread/start", {"cwd": str(cwd), "ephemeral": True,
                "sandbox": "read-only", "approvalPolicy": "on-request", "historyMode": "legacy",
                "environments": [], "dynamicTools": [spec]})
            checks["taggedSpecAccepted"] = "result" in result
            thread_id = result["result"]["thread"]["id"]
            result = probe.request("turn/start", {"threadId": thread_id, "environments": [],
                "input": [{"type": "text", "text": "Synthetic dynamic tool compatibility check."}]})
            completed = probe.wait_turn(thread_id, result["result"]["turn"]["id"])
            checks["turnCompleted"] = completed.get("status") == "completed"
            checks["singleValidCall"] = probe.tool_calls == 1 and probe.valid_call
            checks["restrictedOfferedSurface"] = bool(model.requests) and capabilities.tools_in(model.requests[0]) == ["devhub_git_status", "request_user_input"]
            outputs = [item for request in model.requests[1:] for item in request.get("input", [])
                       if item.get("type") == "function_call_output"]
            checks["responseConsumed"] = any("isRepository" in json.dumps(item) for item in outputs)
        except (RuntimeError, OSError, KeyError, ValueError, queue.Empty):
            checks["protocolCompleted"] = False
        finally:
            if probe is not None: probe.close()
            model.close()
    evidence = {"format": 1, "codexVersion": args.version, "realInference": False,
        "authenticated": False, "releaseEligible": False, "checks": checks,
        "fixtureScopePassed": bool(checks) and all(checks.values())}
    args.output.write_text(json.dumps(evidence, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    print(json.dumps(evidence, sort_keys=True))
    return int(not evidence["fixtureScopePassed"])


if __name__ == "__main__":
    raise SystemExit(main())
