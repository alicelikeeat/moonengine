#!/usr/bin/env python3
"""Validate ACP schemas and their examples.

Every file in protocols/acp/examples/<name>/ must validate against
protocols/acp/schemas/<name>.schema.json. Files under
protocols/acp/examples/invalid/<name>/ must FAIL validation, which keeps
the schemas from silently becoming too permissive.

Requires: pip install jsonschema
"""
import json
import pathlib
import sys

try:
    from jsonschema import Draft202012Validator
except ImportError:
    sys.exit("jsonschema is required: pip install jsonschema")

ROOT = pathlib.Path(__file__).resolve().parent.parent / "protocols" / "acp"


def load(path):
    with open(path, encoding="utf-8") as f:
        return json.load(f)


def main():
    failures = 0
    checked = 0
    for schema_path in sorted((ROOT / "schemas").glob("*.schema.json")):
        name = schema_path.name.removesuffix(".schema.json")
        schema = load(schema_path)
        Draft202012Validator.check_schema(schema)
        validator = Draft202012Validator(schema)

        for example in sorted((ROOT / "examples" / name).glob("*.json")):
            checked += 1
            errors = list(validator.iter_errors(load(example)))
            if errors:
                failures += 1
                print(f"FAIL {example.relative_to(ROOT)}: {errors[0].message}")
            else:
                print(f"ok   {example.relative_to(ROOT)}")

        for example in sorted((ROOT / "examples" / "invalid" / name).glob("*.json")):
            checked += 1
            if validator.is_valid(load(example)):
                failures += 1
                print(f"FAIL {example.relative_to(ROOT)}: expected to be rejected")
            else:
                print(f"ok   {example.relative_to(ROOT)} (rejected as expected)")

    print(f"\n{checked} example(s) checked, {failures} failure(s)")
    return 1 if failures or checked == 0 else 0


if __name__ == "__main__":
    sys.exit(main())
