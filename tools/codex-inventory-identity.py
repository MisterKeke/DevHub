"""Portable identity for exported tool inventories (UTF-8, canonical JSON).

Unlike a file-byte hash this is independent of checkout line endings, indentation
and object-key ordering. Arrays retain their order. Producer and consumer share
this function; changes to tool names, descriptions or schemas still change it.
"""
import hashlib
import json


def inventory_sha256(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(",", ":"),
                                     ensure_ascii=False).encode("utf-8")).hexdigest()
