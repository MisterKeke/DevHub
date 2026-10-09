"""Project generated Codex schemas into deterministic, offline wire fixtures.

No Codex executable, network, credentials or third-party Python modules needed.
The full CLI-generated snapshots stay in the ignored evidence directory.
"""

import argparse
import hashlib
import json
from pathlib import Path


CLIENT_METHODS = (
    "initialize", "thread/start", "thread/resume", "thread/read", "turn/start",
    "turn/interrupt", "account/read", "account/login/start", "account/login/cancel",
    "model/list", "config/read", "configRequirements/read", "experimentalFeature/list",
    "hooks/list", "mcpServerStatus/list", "plugin/list",
)
NOTIFICATION_METHODS = (
    "thread/started", "turn/started", "turn/completed", "item/started", "item/completed",
    "item/agentMessage/delta", "account/updated", "account/login/completed",
)
RESPONSE_NAMES = (
    "InitializeResponse", "ThreadStartResponse", "ThreadResumeResponse", "ThreadReadResponse",
    "TurnStartResponse", "TurnInterruptResponse", "GetAccountResponse", "LoginAccountResponse",
    "CancelLoginAccountResponse", "ModelListResponse", "ConfigReadResponse",
    "ConfigRequirementsReadResponse", "ExperimentalFeatureListResponse", "HooksListResponse",
    "ListMcpServerStatusResponse", "PluginListResponse", "CommandExecutionRequestApprovalResponse",
    "FileChangeRequestApprovalResponse", "PermissionsRequestApprovalResponse",
    "ToolRequestUserInputResponse", "McpServerElicitationRequestResponse", "DynamicToolCallResponse",
    "ExecCommandApprovalResponse", "ApplyPatchApprovalResponse",
)


def canonical(value):
    return json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=False).encode("utf-8")


def strip_annotations(value):
    if isinstance(value, dict):
        result = {}
        for key, child in value.items():
            if key in ("description", "title", "$schema"):
                continue
            if key in ("properties", "definitions", "$defs"):
                result[key] = {name: strip_annotations(shape) for name, shape in child.items()}
            elif key in ("default", "enum", "const", "examples"):
                result[key] = child
            else:
                result[key] = strip_annotations(child)
        return result
    if isinstance(value, list):
        return [strip_annotations(v) for v in value]
    return value


def references(value):
    if isinstance(value, dict):
        if "$ref" in value:
            ref = value["$ref"]
            if not ref.startswith("#/definitions/"):
                raise ValueError("Unexpected schema reference: " + ref)
            yield ref.removeprefix("#/definitions/")
        for child in value.values():
            yield from references(child)
    elif isinstance(value, list):
        for child in value:
            yield from references(child)


def snapshot(root):
    schemas = {path.relative_to(root).as_posix(): json.loads(path.read_text(encoding="utf-8"))
               for path in sorted(root.rglob("*.json"))}
    selected = {}
    needed_definitions = {}

    def add(label, shape, definitions):
        shape = strip_annotations(shape)
        selected[label] = shape
        pending = list(references(shape))
        seen = set()
        while pending:
            name = pending.pop()
            if name in seen:
                continue
            seen.add(name)
            definition = strip_annotations(definitions[name])
            if name in needed_definitions and needed_definitions[name] != definition:
                raise ValueError("Conflicting schema definition: " + name)
            needed_definitions[name] = definition
            pending.extend(references(definition))

    for filename, methods in (("ClientRequest.json", CLIENT_METHODS),
                              ("ServerNotification.json", NOTIFICATION_METHODS),
                              ("ServerRequest.json", None),
                              ("ClientNotification.json", ("initialized",))):
        schema = schemas[filename]
        for variant in schema["oneOf"]:
            method = variant["properties"]["method"]["enum"][0]
            if methods is None or method in methods:
                add(filename.removesuffix(".json") + ":" + method,
                    variant, schema.get("definitions", {}))

    for name in RESPONSE_NAMES + ("RequestId", "JSONRPCResponse", "JSONRPCError"):
        matches = [(path, schema) for path, schema in schemas.items()
                   if path.rsplit("/", 1)[-1] == name + ".json"]
        if len(matches) != 1:
            raise ValueError("Missing or ambiguous schema: " + name)
        path, schema = matches[0]
        add("Response:" + name, {k: v for k, v in schema.items() if k != "definitions"},
            schema.get("definitions", {}))

    return {"format": 1, "codexVersion": "0.160.1", "definitions": needed_definitions,
            "contracts": selected}, {
                path: hashlib.sha256(canonical(schema)).hexdigest()
                for path, schema in schemas.items()}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--schema-root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    manifest = {"format": 1, "codexVersion": "0.160.1", "hashAlgorithm": "sha256-canonical-json",
                "snapshots": {}}
    for mode in ("stable", "experimental"):
        contract, hashes = snapshot(args.schema_root / ("schema-" + mode))
        text = json.dumps(contract, indent=2, sort_keys=True, ensure_ascii=False) + "\n"
        (args.output / (mode + ".json")).write_text(text, encoding="utf-8")
        manifest["snapshots"][mode] = {"files": hashes, "contractSha256":
                                      hashlib.sha256(canonical(contract)).hexdigest()}
        print(f"{mode}: {len(hashes)} schemas, {len(contract['contracts'])} contracts, "
              f"{len(contract['definitions'])} shared definitions")
    (args.output / "manifest.json").write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
