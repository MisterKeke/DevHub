"""Offline Phase 0 checks; no installed Codex, Qt build, network or credentials required."""

import hashlib
import importlib.util
import json
from pathlib import Path
import unittest
from unittest.mock import patch
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[1]
FIXTURES = ROOT / "tests" / "fixtures" / "codex" / "0.160.1"
ALPHA_FIXTURES = ROOT / "tests" / "fixtures" / "codex" / "0.162.0-alpha.17.2"
spec = importlib.util.spec_from_file_location("freeze_codex_contract", ROOT / "tools" / "freeze-codex-contract.py")
freeze = importlib.util.module_from_spec(spec)
spec.loader.exec_module(freeze)
capability_spec = importlib.util.spec_from_file_location("verify_codex_capabilities", ROOT / "tools" / "verify-codex-capabilities.py")
capabilities = importlib.util.module_from_spec(capability_spec)
capability_spec.loader.exec_module(capabilities)
identity_spec = importlib.util.spec_from_file_location("inventory_identity", ROOT / "tools" / "codex-inventory-identity.py")
inventory_identity = importlib.util.module_from_spec(identity_spec)
identity_spec.loader.exec_module(inventory_identity)


def nodes(value):
    if isinstance(value, dict):
        yield value
        for child in value.values():
            yield from nodes(child)
    elif isinstance(value, list):
        for child in value:
            yield from nodes(child)


class CodexContractTest(unittest.TestCase):
    def test_hook_cli_never_inherits_desktop_input(self):
        # Desktop QProcess keeps stdin open. Inheriting it makes `codex exec`
        # wait for EOF instead of processing the explicit synthetic prompt.
        with tempfile.TemporaryDirectory() as temporary, \
                patch.object(capabilities, "ScriptedModel") as model, \
                patch.object(capabilities.subprocess, "run") as run:
            model.return_value.overrides.return_value = ()
            model.return_value.requests = [{}]
            run.return_value = subprocess.CompletedProcess([], 0)
            result = capabilities.run_case(Path("fixture-codex"), Path(temporary),
                "cli-hook-disabled-control", (), [], cli_hook_control=True)
            self.assertIs(run.call_args.kwargs["stdin"], subprocess.DEVNULL)
            self.assertEqual(result["exitCode"], 0)
            self.assertEqual(result["modelRequestCount"], 1)
            model.return_value.close.assert_called_once()

    def test_hook_cli_timeout_produces_failed_evidence(self):
        with tempfile.TemporaryDirectory() as temporary, \
                patch.object(capabilities, "ScriptedModel") as model, \
                patch.object(capabilities.subprocess, "run", side_effect=subprocess.TimeoutExpired("fixture", 15)):
            model.return_value.overrides.return_value = ()
            model.return_value.requests = []
            result = capabilities.run_case(Path("fixture-codex"), Path(temporary),
                "cli-hook-disabled-control", (), [], cli_hook_control=True)
            self.assertEqual(result["failure"], "hook-control-timeout")
            self.assertNotIn("exitCode", result)
            self.assertFalse(result["hookStarted"])
            model.return_value.close.assert_called_once()

    def test_hook_controls_require_completed_model_round_trip(self):
        for key, value in (("exitCode", 1), ("modelRequestCount", 0), ("failure", "hook-control-timeout")):
            cases = json.loads((FIXTURES / "capability-verification.json").read_text(encoding="utf-8"))["cases"]
            next(case for case in cases if case["case"] == "cli-hook-disabled-control")[key] = value
            checks = capabilities.evaluate_cases(cases)
            self.assertFalse(checks["hookCliPositiveAndNegativeControl"])

    def test_phase9_native_inventory_evidence(self):
        path = ALPHA_FIXTURES / "phase9-tool-specifications.json"
        specs = json.loads(path.read_text(encoding="utf-8"))
        evidence = json.loads((ALPHA_FIXTURES / "phase9-offline-tool-verification.json").read_text(encoding="utf-8"))
        self.assertEqual(evidence["codexVersion"], "0.162.0-alpha.17.2")
        self.assertFalse(evidence["authenticated"])
        self.assertFalse(evidence["realInference"])
        self.assertFalse(evidence["releaseEligible"])
        self.assertTrue(evidence["fixtureScopePassed"])
        self.assertTrue(all(evidence["checks"].values()))
        self.assertEqual(evidence["inventoryHashAlgorithm"], "sha256-canonical-json-v1")
        self.assertEqual(evidence["inventorySha256"], inventory_identity.inventory_sha256(specs))
        self.assertEqual(evidence["toolInventory"], [item["name"] for item in specs])
        self.assertEqual(len(specs), 14)
        self.assertEqual(len({item["name"] for item in specs}), 14)
        for item in specs:
            self.assertEqual(item["type"], "function")
            self.assertFalse(item["inputSchema"]["additionalProperties"])
            self.assertFalse({"command", "shell", "argv", "root", "accountId", "effect"}
                             & set(item["inputSchema"]["properties"]))

    def test_inventory_identity_is_portable_and_detects_schema_changes(self):
        specs = json.loads((ALPHA_FIXTURES / "phase9-tool-specifications.json").read_text(encoding="utf-8"))
        lf = json.dumps(specs, indent=2, ensure_ascii=False) + "\n"
        crlf = lf.replace("\n", "\r\n")
        self.assertEqual(inventory_identity.inventory_sha256(json.loads(lf)),
                         inventory_identity.inventory_sha256(json.loads(crlf)))
        changed = json.loads(lf)
        changed[0]["inputSchema"]["additionalProperties"] = True
        self.assertNotEqual(inventory_identity.inventory_sha256(specs), inventory_identity.inventory_sha256(changed))

    def test_schema_layout_normalization(self):
        schemas = {"method.json": {"$defs": {"Identifier": {"type": "string"}},
                                   "properties": {"id": {"$ref": "#/$defs/Identifier"}}},
                   "other.json": {"properties": {"id": {"$ref": "method.json#/$defs/Identifier"}}}}
        normalized = freeze.normalize_bundle(schemas)
        self.assertEqual(normalized["other.json"]["properties"]["id"], {"$ref": "#/definitions/Identifier"})
        self.assertEqual(normalized["other.json"]["definitions"]["Identifier"], {"type": "string"})
        with self.assertRaises(ValueError):
            freeze.normalize_bundle({"bad.json": {"$ref": "https://example.com/schema.json"}})

    @classmethod
    def setUpClass(cls):
        cls.snapshots = {mode: json.loads((FIXTURES / (mode + ".json")).read_text(encoding="utf-8"))
                         for mode in ("stable", "experimental")}
        cls.manifest = json.loads((FIXTURES / "manifest.json").read_text(encoding="utf-8"))
        cls.alpha_snapshots = {mode: json.loads((ALPHA_FIXTURES / (mode + ".json")).read_text(encoding="utf-8"))
                               for mode in ("stable", "experimental")}
        cls.alpha_manifest = json.loads((ALPHA_FIXTURES / "manifest.json").read_text(encoding="utf-8"))

    def test_pinned_hashes_and_reference_closure(self):
        for mode, snapshot in self.snapshots.items():
            with self.subTest(mode=mode):
                self.assertEqual(snapshot["codexVersion"], "0.160.1")
                digest = hashlib.sha256(freeze.canonical(snapshot)).hexdigest()
                self.assertEqual(digest, self.manifest["snapshots"][mode]["contractSha256"])
                for ref in freeze.references(snapshot):
                    self.assertIn(ref, snapshot["definitions"])

    def test_required_fields_survive_annotation_projection(self):
        for mode, snapshot in self.snapshots.items():
            for node in nodes(snapshot):
                if "properties" in node and isinstance(node.get("required"), list):
                    with self.subTest(mode=mode, fields=node["required"]):
                        self.assertTrue(set(node["required"]).issubset(node["properties"]))
        original = {"description": "annotation", "properties": {
            "description": {"description": "annotation", "type": "string"},
            "title": {"type": "string"}}, "required": ["description", "title"]}
        projected = freeze.strip_annotations(original)
        self.assertNotIn("description", projected)
        self.assertEqual(set(projected["properties"]), {"description", "title"})

    def test_alpha_schema_projection_and_contract_compatibility(self):
        for mode, snapshot in self.alpha_snapshots.items():
            with self.subTest(mode=mode):
                self.assertEqual(snapshot["codexVersion"], "0.162.0-alpha.17.2")
                self.assertEqual(hashlib.sha256(freeze.canonical(snapshot)).hexdigest(),
                                 self.alpha_manifest["snapshots"][mode]["contractSha256"])
                for method in ("initialize", "thread/start", "thread/resume", "thread/read", "turn/start",
                               "turn/interrupt", "account/read", "account/login/start", "account/login/cancel",
                               "model/list", "config/read", "configRequirements/read"):
                    old = self.snapshots["experimental"]["contracts"]["ClientRequest:" + method]
                    alpha = snapshot["contracts"]["ClientRequest:" + method]
                    self.assertEqual(old, alpha, method)
                old_requirements = self.snapshots["experimental"]["contracts"][
                    "Response:ConfigRequirementsReadResponse"]
                alpha_requirements = snapshot["contracts"]["Response:ConfigRequirementsReadResponse"]
                self.assertEqual(old_requirements["properties"]["requirements"],
                                 alpha_requirements["properties"]["requirements"])
                self.assertEqual(alpha_requirements["properties"]["supportsIndependentSpeedModes"]["type"],
                                 ["boolean", "null"])
                if mode == "experimental":
                    for name in ("DynamicToolSpec", "DynamicToolCallParams", "DynamicToolCallOutputContentItem"):
                        self.assertEqual(snapshot["definitions"][name], self.snapshots[mode]["definitions"][name])
                    for name in ("ServerRequest:item/tool/call", "Response:DynamicToolCallResponse"):
                        self.assertEqual(snapshot["contracts"][name], self.snapshots[mode]["contracts"][name])
                for name in ("TurnStartParams", "Turn"):
                    self.assertIn("parentTurnId" if name == "TurnStartParams" else "rootTurnId",
                                  snapshot["definitions"][name]["properties"])

    def test_alpha_native_offline_read_tool_evidence(self):
        evidence = json.loads((ALPHA_FIXTURES / "offline-read-tool-verification.json").read_text(encoding="utf-8"))
        self.assertEqual(evidence["codexVersion"], "0.162.0-alpha.17.2")
        self.assertFalse(evidence["realInference"])
        self.assertFalse(evidence["authenticated"])
        self.assertFalse(evidence["releaseEligible"])
        self.assertTrue(evidence["fixtureScopePassed"])
        self.assertEqual(set(evidence["checks"]), {"isolatedHome", "responseConsumed", "restrictedOfferedSurface",
                                                "singleValidCall", "taggedSpecAccepted", "turnCompleted"})
        self.assertTrue(all(evidence["checks"].values()))

    def test_required_client_methods_and_rpc_envelope(self):
        for snapshot in self.snapshots.values():
            for method in freeze.CLIENT_METHODS:
                request = snapshot["contracts"]["ClientRequest:" + method]
                expected = {"id", "method"} if method == "configRequirements/read" else {"id", "method", "params"}
                self.assertEqual(set(request["required"]), expected)
                self.assertNotIn("jsonrpc", request["properties"])
            self.assertEqual(snapshot["contracts"]["ClientRequest:configRequirements/read"]
                             ["properties"]["params"], {"type": "null"})
            variants = snapshot["definitions"]["RequestId"]["anyOf"]
            self.assertEqual({v["type"] for v in variants}, {"string", "integer"})

    def test_experimental_environment_and_tool_contract(self):
        stable = self.snapshots["stable"]["definitions"]
        experimental = self.snapshots["experimental"]["definitions"]
        for name in ("ThreadStartParams", "TurnStartParams"):
            self.assertNotIn("environments", stable[name]["properties"])
            self.assertEqual(experimental[name]["properties"]["environments"]["type"], ["array", "null"])
        self.assertNotIn("dynamicTools", stable["ThreadStartParams"]["properties"])
        self.assertIn("dynamicTools", experimental["ThreadStartParams"]["properties"])
        self.assertNotIn("dynamicTools", experimental["ThreadResumeParams"]["properties"])
        tool_types = {v["properties"]["type"]["enum"][0]
                      for v in experimental["DynamicToolSpec"]["oneOf"]}
        self.assertEqual(tool_types, {"function", "namespace"})
        response = self.snapshots["experimental"]["contracts"]["Response:DynamicToolCallResponse"]
        self.assertEqual(set(response["required"]), {"contentItems", "success"})

    def test_identity_and_terminal_status(self):
        for snapshot in self.snapshots.values():
            definitions = snapshot["definitions"]
            self.assertEqual(set(definitions["AgentMessageDeltaNotification"]["required"]),
                             {"threadId", "turnId", "itemId", "delta"})
            self.assertEqual(set(definitions["TurnInterruptParams"]["required"]), {"threadId", "turnId"})
            self.assertEqual(set(definitions["TurnStatus"]["enum"]),
                             {"completed", "failed", "interrupted", "inProgress"})
            agent = next(v for v in definitions["ThreadItem"]["oneOf"]
                         if v["properties"]["type"].get("enum") == ["agentMessage"])
            self.assertEqual(set(agent["required"]), {"id", "text", "type"})

    def test_public_account_binding_fields(self):
        stable = self.snapshots["stable"]["contracts"]["Response:GetAccountResponse"]
        self.assertNotIn("workspaceRouting", stable["properties"])
        for snapshot in (self.snapshots["experimental"],):
            definitions = snapshot["definitions"]
            self.assertIn("workspaceRouting", snapshot["contracts"]["Response:GetAccountResponse"]["properties"])
            self.assertTrue({"backendOrigin", "chatgptAccountId"}.issubset(
                definitions["WorkspaceRouting"]["required"]))
            chatgpt = next(v for v in definitions["Account"]["oneOf"]
                           if v["properties"]["type"].get("enum") == ["chatgpt"])
            self.assertEqual(chatgpt["properties"]["email"]["type"], ["string", "null"])

    def test_native_approval_is_not_a_tool_result(self):
        snapshot = self.snapshots["experimental"]
        for name in ("CommandExecutionRequestApprovalResponse", "FileChangeRequestApprovalResponse"):
            self.assertEqual(snapshot["contracts"]["Response:" + name]["required"], ["decision"])
        methods = {key.removeprefix("ServerRequest:") for key in snapshot["contracts"]
                   if key.startswith("ServerRequest:")}
        self.assertTrue({"item/commandExecution/requestApproval", "item/fileChange/requestApproval",
                         "item/permissions/requestApproval", "mcpServer/elicitation/request",
                         "item/tool/requestUserInput", "item/tool/call"}.issubset(methods))

    def test_runtime_evidence_keeps_profile_rejected(self):
        for name in ("inventory-stable", "inventory-experimental", "inventory-adversarial"):
            evidence = json.loads((FIXTURES / (name + ".json")).read_text(encoding="utf-8"))
            self.assertFalse(evidence["releaseEligible"])
            self.assertFalse(evidence["inferencePerformed"])
            self.assertTrue(evidence["isolatedHome"])
            self.assertTrue(evidence["requests"]["initialize"]["isolatedHomeConfirmed"])
            self.assertNotIn("probeFailure", evidence)
            self.assertTrue(evidence["requests"]["config/read"]["disabledFeatures"]["unified_exec"])
            self.assertTrue(evidence["requests"]["experimentalFeature/list"]["features"]["unified_exec"])
        stable = json.loads((FIXTURES / "inventory-stable.json").read_text(encoding="utf-8"))
        self.assertEqual(stable["requests"]["thread/start"], {"ok": False, "errorCode": -32600})
        adversarial = json.loads((FIXTURES / "inventory-adversarial.json").read_text(encoding="utf-8"))
        self.assertEqual(adversarial["sentinels"], {"hookStarted": False, "mcpStarted": True})
        self.assertEqual(adversarial["requests"]["thread/start"]["environmentCount"], 0)
        self.assertEqual(adversarial["requests"]["mcpServerStatus/list@thread"]["serverCount"], 1)
        self.assertEqual(adversarial["requests"]["mcpServerStatus/list@thread"]["toolCount"], 1)
        self.assertEqual(adversarial["requests"]["mcpServerStatus/list@thread"]["toolsErrorCount"], 0)

    def test_capability_controls_and_injections(self):
        evidence = json.loads((FIXTURES / "capability-verification.json").read_text(encoding="utf-8"))
        self.assertEqual(evidence["codexVersion"], "0.160.1")
        self.assertFalse(evidence["realInference"])
        self.assertFalse(evidence["releaseEligible"])
        self.assertEqual(len(evidence["cases"]), 27)
        self.assertEqual(capabilities.evaluate_cases(evidence["cases"]), evidence["checks"])
        self.assertTrue(all(evidence["checks"].values()))
        self.assertTrue(evidence["fixtureScopePassed"])
        for case in evidence["cases"]:
            self.assertFalse(case["realInference"])
            self.assertNotIn("failure", case)
            if case["case"].startswith("hardened-"):
                self.assertEqual(case["configurationAdmissionReasons"], [])
                self.assertEqual(case["offeredTools"], ["request_user_input"])
                self.assertTrue(case["disabledToolRejected"])
                self.assertFalse(any(case["preThreadSentinels"].values()))
                self.assertFalse(any(case["sentinels"].values()))

    def test_admission_rejects_inherited_and_managed_execution(self):
        features = dict.fromkeys(("shell_tool", "unified_exec", "apps", "plugins", "hooks",
                                  "browser_use", "computer_use", "goals", "image_generation"), False)
        config = {"features": features, "web_search": "disabled", "notify": [],
                  "mcp_servers": {"known": {"enabled": False}}}
        self.assertEqual(capabilities.admission_reasons(config, None), [])
        # These are policies we cannot silently override or inherit into text-only chat.
        self.assertIn("managed-hooks-not-verified", capabilities.admission_reasons(config, {"hooks": {"SessionStart": [{}]}}))
        self.assertIn("managed-feature-conflict:hooks", capabilities.admission_reasons(config, {"featureRequirements": {"hooks": True}}))
        self.assertIn("inherited-mcp-enabled", capabilities.admission_reasons({**config, "mcp_servers": {"new": {}}}, None))
        self.assertIn("legacy-notify-enabled", capabilities.admission_reasons({**config, "notify": ["unreviewed-command"]}, None))
        self.assertIn("feature-not-disabled:plugins", capabilities.admission_reasons({**config, "features": {**features, "plugins": True}}, None))
        for malformed in (None, [], {"features": []}, {"features": features, "mcp_servers": ["unknown"]}):
            self.assertTrue(capabilities.admission_reasons(malformed, None))
        self.assertTrue(capabilities.admission_reasons(config, ["unknown"]))


if __name__ == "__main__":
    unittest.main()
