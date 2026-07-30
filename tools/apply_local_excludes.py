#!/usr/bin/env python3
"""Idempotently hide local VR patch/debug artifacts from Git status."""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path


BEGIN = "# BEGIN KISAKCOD VR LOCAL DEVELOPMENT ARTIFACTS"
END = "# END KISAKCOD VR LOCAL DEVELOPMENT ARTIFACTS"


def fail(message: str) -> "NoReturn":
    print(f"ERROR: {message}", file=sys.stderr)
    raise SystemExit(1)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "repository",
        nargs="?",
        default=".",
        help="KisakCOD repository root (default: current directory)",
    )
    args = parser.parse_args()

    requested = Path(args.repository).resolve()
    result = subprocess.run(
        ["git", "-C", str(requested), "rev-parse", "--show-toplevel"],
        check=False,
        capture_output=True,
        text=True,
    )
    if result.returncode:
        fail(result.stderr.strip() or f"{requested} is not a Git repository")

    root = Path(result.stdout.strip())
    template = root / "tools" / "local-git-exclude-additions.txt"
    if not template.is_file():
        fail(f"missing template: {template}")

    block = template.read_text(encoding="utf-8").strip()
    if not block.startswith(BEGIN) or not block.endswith(END):
        fail("the local exclude template has invalid boundary markers")

    target = root / ".git" / "info" / "exclude"
    if not target.parent.is_dir():
        fail(f"missing Git metadata directory: {target.parent}")

    existing = target.read_text(encoding="utf-8") if target.exists() else ""
    if BEGIN in existing or END in existing:
        if BEGIN in existing and END in existing:
            print("Local KisakCOD VR excludes are already installed.")
            return 0
        fail("only one local exclude boundary marker is present; repair manually")

    separator = "" if not existing or existing.endswith("\n") else "\n"
    target.write_text(
        existing + separator + block + "\n",
        encoding="utf-8",
        newline="\n",
    )
    print(f"Installed local excludes in {target}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
