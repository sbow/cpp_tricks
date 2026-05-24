#!/usr/bin/env python3
"""Verify README.md documents the current repo layout and tooling."""

from __future__ import annotations

import sys
from pathlib import Path


def main() -> int:
    root = Path.cwd()
    readme_path = root / "README.md"
    if not readme_path.is_file():
        print("FAIL: README.md not found")
        return 1

    readme = readme_path.read_text(encoding="utf-8")
    errors: list[str] = []

    required_snippets = [
        ("Makefile / make", lambda t: "make" in t.lower()),
        ("interrogate.py", lambda t: "interrogate.py" in t),
        ("cpp_tricks/ layout", lambda t: "cpp_tricks/" in t),
        ("Prerequisites section", lambda t: "## Prerequisites" in t),
        ("Features table", lambda t: "## Features" in t and "| Feature |" in t),
    ]
    for label, pred in required_snippets:
        if not pred(readme):
            errors.append(f"README missing: {label}")

    programs_root = root / "cpp_tricks"
    programs = sorted(
        src.parent.parent.name
        for src in programs_root.glob("*/src/*.cpp")
    )
    for program in programs:
        if program not in readme:
            errors.append(f"README does not mention program: {program}")

    if (programs_root / "ipc").is_dir() and "ipc" not in readme.lower():
        errors.append("README does not mention ipc (header-only library + tests)")

    makefile_targets = ["test-ipc", "test-ipc-mp"]
    for target in makefile_targets:
        if target not in readme:
            errors.append(f"README missing make target: {target}")

    if "--programs-root" not in readme and "--source" in readme:
        errors.append(
            "README interrogate docs look outdated "
            "(uses --source but not --programs-root)"
        )

    if errors:
        print("README check failed:")
        for err in errors:
            print(f"  - {err}")
        return 1

    print("README check passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
