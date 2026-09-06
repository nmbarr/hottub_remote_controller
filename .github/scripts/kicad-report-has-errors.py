#!/usr/bin/env python3
"""Exit 1 if a KiCad ERC/DRC JSON report contains any error-severity violation.

Warning-severity violations (e.g. library-resolution warnings from a CI
container that doesn't have the same installed libraries as a developer's
machine) are left out of the pass/fail decision, since they aren't
schematic/board defects.
"""
import json
import sys


def has_error(obj):
    if isinstance(obj, dict):
        if obj.get("severity") == "error":
            return True
        return any(has_error(v) for v in obj.values())
    if isinstance(obj, list):
        return any(has_error(v) for v in obj)
    return False


if __name__ == "__main__":
    with open(sys.argv[1]) as f:
        data = json.load(f)
    sys.exit(1 if has_error(data) else 0)
