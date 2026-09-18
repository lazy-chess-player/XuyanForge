#!/usr/bin/env python3
"""Small dependency-free validator for the checked-in M0 contract fixtures.

It intentionally implements only the JSON Schema keywords used by this repository.
The schemas remain the authoritative exchange artifacts; production validation will
be locked to a full draft-2020-12 implementation before external packages are accepted.
"""

from __future__ import annotations

import json
import sys
from pathlib import Path
from typing import Any


def matches_type(value: Any, expected: str) -> bool:
    return {
        "object": isinstance(value, dict),
        "array": isinstance(value, list),
        "string": isinstance(value, str),
        "integer": isinstance(value, int) and not isinstance(value, bool),
        "boolean": isinstance(value, bool),
        "null": value is None,
    }[expected]


def validate(value: Any, schema: dict[str, Any], path: str = "$") -> list[str]:
    errors: list[str] = []
    expected = schema.get("type")
    if expected is not None:
        types = expected if isinstance(expected, list) else [expected]
        if not any(matches_type(value, kind) for kind in types):
            return [f"{path}: expected {types}"]
    if "const" in schema and value != schema["const"]:
        errors.append(f"{path}: value must equal {schema['const']!r}")
    if "enum" in schema and value not in schema["enum"]:
        errors.append(f"{path}: value is outside enum")
    if isinstance(value, str):
        if len(value) < schema.get("minLength", 0):
            errors.append(f"{path}: string is too short")
        if len(value) > schema.get("maxLength", sys.maxsize):
            errors.append(f"{path}: string is too long")
    if isinstance(value, int) and not isinstance(value, bool) and value < schema.get("minimum", value):
        errors.append(f"{path}: number is below minimum")
    if isinstance(value, list):
        if len(value) < schema.get("minItems", 0):
            errors.append(f"{path}: array is too short")
        if len(value) > schema.get("maxItems", sys.maxsize):
            errors.append(f"{path}: array is too long")
        if "items" in schema:
            for index, item in enumerate(value):
                errors.extend(validate(item, schema["items"], f"{path}[{index}]"))
    if isinstance(value, dict):
        for required in schema.get("required", []):
            if required not in value:
                errors.append(f"{path}: missing {required}")
        properties = schema.get("properties", {})
        if schema.get("additionalProperties") is False:
            for name in value:
                if name not in properties:
                    errors.append(f"{path}: unexpected property {name}")
        for name, child in value.items():
            if name in properties:
                errors.extend(validate(child, properties[name], f"{path}.{name}"))
    return errors


def main() -> int:
    root = Path(__file__).resolve().parents[1]
    schemas = list((root / "contracts").glob("*.schema.json"))
    for path in schemas:
        json.loads(path.read_text(encoding="utf-8"))
    command_schema = json.loads((root / "contracts/command.schema.json").read_text(encoding="utf-8"))
    valid = json.loads((root / "fixtures/contracts/valid-command.json").read_text(encoding="utf-8"))
    invalid = json.loads((root / "fixtures/contracts/invalid-command.json").read_text(encoding="utf-8"))
    valid_errors = validate(valid, command_schema)
    invalid_errors = validate(invalid, command_schema)
    if valid_errors:
        print("Valid fixture rejected:", *valid_errors, sep="\n", file=sys.stderr)
        return 1
    if not invalid_errors:
        print("Invalid fixture unexpectedly accepted", file=sys.stderr)
        return 1
    intent_schema = json.loads((root / "contracts/intent.schema.json").read_text(encoding="utf-8"))
    valid_intent = json.loads((root / "fixtures/contracts/valid-intent.json").read_text(encoding="utf-8"))
    invalid_intent = json.loads((root / "fixtures/contracts/invalid-intent.json").read_text(encoding="utf-8"))
    intent_errors = validate(valid_intent, intent_schema)
    invalid_intent_errors = validate(invalid_intent, intent_schema)
    if intent_errors:
        print("Valid intent fixture rejected:", *intent_errors, sep="\n", file=sys.stderr)
        return 1
    if not invalid_intent_errors:
        print("Invalid intent fixture unexpectedly accepted", file=sys.stderr)
        return 1
    print(f"Validated {len(schemas)} schemas; command and intent fixtures behaved as expected.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
