#!/usr/bin/env python3
"""Validate protocol and file-format schemas and their examples.

For every protocols/<area>/ (acp, level, ...): each file in
examples/<name>/ must validate against schemas/<name>.schema.json, and
each file under examples/invalid/<name>/ must FAIL validation, which keeps
the schemas from silently becoming too permissive. Sample levels in
samples/levels/*.alevel are also checked against the level schema.

Requires: pip install jsonschema
"""
import json
import pathlib
import sys

try:
    from jsonschema import Draft202012Validator
except ImportError:
    sys.exit("jsonschema is required: pip install jsonschema")

REPO = pathlib.Path(__file__).resolve().parent.parent
PROTOCOLS = REPO / "protocols"


def load(path):
    with open(path, encoding="utf-8") as f:
        return json.load(f)


def main():
    failures = 0
    checked = 0
    for area in sorted(p for p in PROTOCOLS.iterdir() if (p / "schemas").is_dir()):
        f, c = check_area(area)
        failures += f
        checked += c
    level_schema = Draft202012Validator(load(PROTOCOLS / "level" / "schemas" / "level.schema.json"))
    for sample in sorted((REPO / "samples" / "levels").glob("*.alevel")):
        checked += 1
        errors = list(level_schema.iter_errors(load(sample)))
        if errors:
            failures += 1
            print(f"FAIL {sample.relative_to(REPO)}: {errors[0].message}")
        else:
            print(f"ok   {sample.relative_to(REPO)}")
    print(f"\n{checked} example(s) checked, {failures} failure(s)")
    return 1 if failures or checked == 0 else 0


def check_area(ROOT):
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
                print(f"FAIL {example.relative_to(REPO)}: {errors[0].message}")
            else:
                print(f"ok   {example.relative_to(REPO)}")

        for example in sorted((ROOT / "examples" / "invalid" / name).glob("*.json")):
            checked += 1
            if validator.is_valid(load(example)):
                failures += 1
                print(f"FAIL {example.relative_to(REPO)}: expected to be rejected")
            else:
                print(f"ok   {example.relative_to(REPO)} (rejected as expected)")
    return failures, checked


if __name__ == "__main__":
    sys.exit(main())
