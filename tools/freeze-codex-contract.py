"""Project generated Codex schemas into deterministic, offline wire fixtures.

No Codex executable, network, credentials or third-party Python modules needed.
The full CLI-generated snapshots stay in the ignored evidence directory.
"""

import argparse
import hashlib
import json
from pathlib import Path
from pathlib import PurePosixPath


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


def normalize_bundle(schemas):
    """Normalize local $defs and file references; never fetch external schemas.

    Named definitions retain their old names when unambiguous, preserving the
    existing fixture format. Conflicting definitions and unsupported references
    fail explicitly rather than feeding new constructs to the limited validator.
    """
    definitions = {}
    active = set()
    budget = 200000

    def pointer(filename, fragment):
        value = schemas[filename]
        if fragment and not fragment.startswith("/"):
            raise ValueError("Schema anchors are unsupported: " + fragment)
        for part in fragment.split("/")[1:]:
            value = value[part.replace("~1", "/").replace("~0", "~")]
        return value

    def normalize(value, filename, depth=0):
        nonlocal budget
        budget -= 1
        if budget < 0 or depth > 96:
            raise ValueError("Schema normalization limit exceeded")
        if isinstance(value, list):
            return [normalize(child, filename, depth + 1) for child in value]
        if not isinstance(value, dict):
            return value
        result = {}
        for key, child in value.items():
            if key in ("definitions", "$defs"):
                continue
            if key in ("description", "title", "$schema", "$id", "default", "examples"):
                if key in ("default", "examples"):
                    result[key] = child
                continue
            if key == "$ref":
                if not isinstance(child, str) or ":" in child or "\\" in child:
                    raise ValueError("External or invalid reference: " + str(child))
                relative, separator, fragment = child.partition("#")
                target = str(PurePosixPath(filename).parent / relative) if relative else filename
                # Resolve . and .. without touching the filesystem.
                parts = []
                for part in target.split("/"):
                    if part in ("", "."):
                        continue
                    if part == "..":
                        if not parts:
                            raise ValueError("Reference escapes schema bundle")
                        parts.pop()
                    else:
                        parts.append(part)
                target = "/".join(parts)
                if child.startswith("/") or target not in schemas:
                    raise ValueError("Missing local schema reference: " + child)
                if fragment.startswith(("/definitions/", "/$defs/")) and fragment.count("/") == 2:
                    name = fragment.rsplit("/", 1)[-1].replace("~1", "/").replace("~0", "~")
                elif not fragment:
                    name = PurePosixPath(target).stem
                else:
                    raise ValueError("Unsupported reference target: " + child)
                if "/" in name or "~" in name:
                    raise ValueError("Unsupported definition name: " + name)
                identity = (target, fragment)
                if identity not in active:
                    active.add(identity)
                    definition = normalize(pointer(target, fragment), target, depth + 1)
                    active.remove(identity)
                    if name in definitions and definitions[name] != definition:
                        raise ValueError("Conflicting schema definition: " + name)
                    definitions[name] = definition
                result["$ref"] = "#/definitions/" + name
            elif key == "properties":
                result[key] = {name: normalize(shape, filename, depth + 1) for name, shape in child.items()}
            elif key in ("enum", "const"):
                if key == "const":
                    result["enum"] = [child]
                else:
                    result[key] = child
            else:
                result[key] = normalize(child, filename, depth + 1)
        return result

    normalized = {filename: normalize(schema, filename) for filename, schema in schemas.items()}
    # Aggregate layouts can name roots within $defs/definitions.
    for filename, schema in schemas.items():
        for container in ("definitions", "$defs"):
            for name, shape in schema.get(container, {}).items():
                exported = name + ".json"
                if exported not in normalized and name in (*RESPONSE_NAMES, "RequestId", "JSONRPCResponse", "JSONRPCError",
                                                          "ClientRequest", "ServerRequest", "ServerNotification", "ClientNotification"):
                    normalized[exported] = normalize(shape, filename)
    return {filename: {**schema, "definitions": definitions} for filename, schema in normalized.items()}


def snapshot(root, version):
    schemas = {path.relative_to(root).as_posix(): json.loads(path.read_text(encoding="utf-8"))
               for path in sorted(root.rglob("*.json"))}
    hashes = {path: hashlib.sha256(canonical(schema)).hexdigest() for path, schema in schemas.items()}
    schemas = normalize_bundle(schemas)
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
        for variant in schema.get("oneOf", schema.get("anyOf", [])):
            if "$ref" in variant:
                variant = schema["definitions"][variant["$ref"].removeprefix("#/definitions/")]
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

    return {"format": 1, "codexVersion": version, "definitions": needed_definitions,
            "contracts": selected}, hashes


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--schema-root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--version", default="0.160.1")
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=True)
    manifest = {"format": 1, "codexVersion": args.version, "hashAlgorithm": "sha256-canonical-json",
                "snapshots": {}}
    for mode in ("stable", "experimental"):
        contract, hashes = snapshot(args.schema_root / ("schema-" + mode), args.version)
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
