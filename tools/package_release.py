#!/usr/bin/env python3
"""Build a strict, source-linked KisakCOD VR Windows release ZIP."""

from __future__ import annotations

import argparse
import hashlib
import os
import re
import subprocess
import sys
import tempfile
import zipfile
from datetime import datetime, timezone
from pathlib import Path
from typing import NoReturn


VERSION_RE = re.compile(r"^\d+\.\d+\.\d+(?:-[0-9A-Za-z][0-9A-Za-z.-]*)?$")
FORBIDDEN_SUFFIXES = {
    ".dll",
    ".pdb",
    ".iwd",
    ".ff",
    ".d3dbsp",
    ".svg",
    ".log",
}
PACKAGE_TEMPLATES = (
    "Launch-KisakCOD-VR.bat",
    "Launch-KisakCOD-VR-Diagnostics.bat",
    "VR-Settings.bat",
    "README-FIRST.txt",
    "licenses/Tracy-LICENSE.txt",
)
RELEASE_BLOCKERS = (
    "GITHUB_USERNAME_HERE",
    "PATREON_URL_HERE",
    "DEATH_FROM_ABOVE_SKIP_INSTRUCTIONS_HERE",
    "CONTROLLER_MAPPING_MUST_BE_VERIFIED_HERE",
)


def fail(message: str) -> NoReturn:
    print(f"ERROR: {message}", file=sys.stderr)
    raise SystemExit(1)


def git(root: Path, *args: str, allow_failure: bool = False) -> str:
    result = subprocess.run(
        ["git", "-C", str(root), *args],
        check=False,
        capture_output=True,
        text=True,
    )
    if result.returncode and not allow_failure:
        detail = result.stderr.strip() or result.stdout.strip()
        fail(f"git {' '.join(args)} failed: {detail}")
    return result.stdout.rstrip()


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def normalized_url(value: str, label: str) -> str:
    value = value.strip().rstrip("/")
    if not value.startswith(("https://", "http://")):
        fail(f"{label} must be an http(s) URL")
    if any(token in value for token in ("HERE", "YOUR_", "example.com")):
        fail(f"{label} still contains a placeholder")
    return value


def read_required(path: Path) -> bytes:
    if not path.is_file():
        fail(f"required release input is missing: {path}")
    return path.read_bytes()


def template_bytes(path: Path, replacements: dict[str, str]) -> bytes:
    text = read_required(path).decode("utf-8")
    for old, new in replacements.items():
        text = text.replace(old, new)
    unresolved = sorted(set(re.findall(r"@[A-Z0-9_]+@", text)))
    if unresolved:
        fail(f"unresolved template token(s) in {path}: {', '.join(unresolved)}")
    return text.replace("\n", "\r\n").encode("utf-8")


def zip_info(name: str) -> zipfile.ZipInfo:
    now = datetime.now().timetuple()
    info = zipfile.ZipInfo(name, date_time=now[:6])
    info.compress_type = zipfile.ZIP_DEFLATED
    info.create_system = 3
    info.external_attr = (0o100644 & 0xFFFF) << 16
    return info


def main() -> int:
    parser = argparse.ArgumentParser(
        description=(
            "Create an allowlisted KisakCOD VR ZIP from bin/Release and link it "
            "to the exact public Git tag."
        )
    )
    parser.add_argument("--version", required=True, help="Example: 0.9.0-beta.1")
    parser.add_argument("--repository-url", required=True)
    parser.add_argument("--patreon-url", required=True)
    parser.add_argument(
        "--output-directory",
        default="releases",
        help="Path relative to the repository root (default: releases)",
    )
    args = parser.parse_args()

    if not VERSION_RE.fullmatch(args.version):
        fail("version must look like 0.9.0-beta.1 and must not start with v")

    repository_url = normalized_url(args.repository_url, "repository URL")
    if repository_url.endswith(".git"):
        repository_url = repository_url[:-4]
    patreon_url = normalized_url(args.patreon_url, "Patreon URL")
    tag = f"v{args.version}"

    script = Path(__file__).resolve()
    root = script.parent.parent
    actual_root = Path(git(root, "rev-parse", "--show-toplevel")).resolve()
    if actual_root != root.resolve():
        fail(f"script is not under the repository root: {actual_root}")

    tracked_status = git(
        root, "status", "--porcelain=v1", "--untracked-files=no"
    )
    if tracked_status:
        fail(
            "tracked files are modified or staged; commit them and rebuild "
            "before packaging:\n" + tracked_status
        )

    head = git(root, "rev-parse", "HEAD")
    tag_commit = git(root, "rev-list", "-n", "1", tag, allow_failure=True)
    if not tag_commit:
        fail(f"required tag {tag} does not exist")
    if tag_commit != head:
        fail(f"{tag} points to {tag_commit}, but HEAD is {head}")

    submodule_status = git(root, "submodule", "status", "--recursive")
    bad_submodules = [
        line for line in submodule_status.splitlines() if line and line[0] != " "
    ]
    if bad_submodules:
        fail(
            "submodules are missing, modified, or on the wrong commit:\n"
            + "\n".join(bad_submodules)
        )

    public_documents = {
        "INSTALL.txt": root / "INSTALL.md",
        "CONTROLS.txt": root / "CONTROLS.md",
        "KNOWN-ISSUES.txt": root / "KNOWN-ISSUES.md",
        "CHANGELOG.txt": root / "CHANGELOG.md",
    }
    documents_to_check = [root / "README.md", *public_documents.values()]
    for document in documents_to_check:
        text = read_required(document).decode("utf-8")
        blockers = [marker for marker in RELEASE_BLOCKERS if marker in text]
        if re.search(r"\bVERIFY\b", text):
            blockers.append("VERIFY")
        if blockers:
            fail(
                f"release documentation is unfinished in {document}: "
                + ", ".join(blockers)
            )

    binary_path = root / "bin" / "Release" / "KisakCOD-sp.exe"
    binary = read_required(binary_path)
    if len(binary) < 1_000_000:
        fail(f"compiled executable looks unexpectedly small: {binary_path}")

    package_root = root / "release" / "package"
    replacements = {
        "@VERSION@": args.version,
        "@TAG@": tag,
        "@COMMIT@": head,
        "@REPOSITORY_URL@": repository_url,
        "@PATREON_URL@": patreon_url,
    }

    payload: dict[str, bytes] = {}
    for relative in PACKAGE_TEMPLATES:
        source = package_root / Path(relative)
        payload[relative] = template_bytes(source, replacements)

    for destination, source in public_documents.items():
        payload[destination] = (
            read_required(source)
            .decode("utf-8")
            .replace("\n", "\r\n")
            .encode("utf-8")
        )

    payload["KisakCOD-sp.exe"] = binary
    payload["LICENSE-GPLv3.txt"] = read_required(root / "LICENSE")
    payload["THIRD-PARTY-NOTICES.txt"] = read_required(
        root / "THIRD-PARTY-NOTICES.md"
    )
    payload["licenses/OpenXR-SDK-LICENSE.txt"] = read_required(
        root / "deps" / "openxr-sdk" / "LICENSE"
    )
    payload["licenses/OpenXR-SDK-Source-LICENSE.txt"] = read_required(
        root / "deps" / "openxr-sdk-source" / "LICENSE"
    )

    source_text = f"""KisakCOD VR v{args.version}

This executable corresponds to the complete source at:
{repository_url}/tree/{tag}

Git commit:
{head}

Clone the exact source and its submodules with:
git clone --recurse-submodules --branch {tag} {repository_url}.git

KisakCOD VR is GPLv3-covered software. See LICENSE-GPLv3.txt.
"""
    payload["SOURCE.txt"] = source_text.replace("\n", "\r\n").encode("utf-8")

    built_at = datetime.now(timezone.utc).replace(microsecond=0).isoformat()
    build_info = f"""Version: {args.version}
Tag: {tag}
Commit: {head}
Packaged UTC: {built_at}
Executable source: bin/Release/KisakCOD-sp.exe
Executable SHA-256: {sha256(binary)}
"""
    payload["BUILD-INFO.txt"] = build_info.replace("\n", "\r\n").encode("utf-8")

    checksum_lines = [
        f"{sha256(data)} *{name}" for name, data in sorted(payload.items())
    ]
    payload["SHA256SUMS.txt"] = (
        "\r\n".join(checksum_lines) + "\r\n"
    ).encode("utf-8")

    expected_names = set(payload)
    for name in expected_names:
        suffix = Path(name).suffix.lower()
        if suffix in FORBIDDEN_SUFFIXES:
            fail(f"forbidden package type reached the allowlist: {name}")
        if Path(name).name.lower() == "iw3sp.exe":
            fail("the original COD4 executable must never be packaged")

    output_dir = (root / args.output_directory).resolve()
    try:
        output_dir.relative_to(root.resolve())
    except ValueError:
        fail("output directory must remain inside the repository")
    output_dir.mkdir(parents=True, exist_ok=True)

    archive = output_dir / f"KisakCOD-VR-v{args.version}.zip"
    sidecar = archive.with_suffix(archive.suffix + ".sha256")
    if archive.exists() or sidecar.exists():
        fail(f"release output already exists; move or remove it first: {archive}")

    temporary_name: str | None = None
    try:
        with tempfile.NamedTemporaryFile(
            prefix=archive.name + ".",
            suffix=".tmp",
            dir=output_dir,
            delete=False,
        ) as temporary:
            temporary_name = temporary.name

        with zipfile.ZipFile(
            temporary_name,
            mode="w",
            compression=zipfile.ZIP_DEFLATED,
            compresslevel=9,
            allowZip64=True,
        ) as package:
            for name, data in sorted(payload.items()):
                package.writestr(zip_info(name), data)

        with zipfile.ZipFile(temporary_name, mode="r") as package:
            actual_names = set(package.namelist())
            if actual_names != expected_names:
                fail(
                    "ZIP inventory mismatch: "
                    f"expected {sorted(expected_names)}, got {sorted(actual_names)}"
                )
            corrupt = package.testzip()
            if corrupt:
                fail(f"ZIP CRC test failed for {corrupt}")

        os.replace(temporary_name, archive)
        temporary_name = None
        archive_hash = sha256(archive.read_bytes())
        sidecar.write_text(
            f"{archive_hash} *{archive.name}\n",
            encoding="ascii",
            newline="\n",
        )
    finally:
        if temporary_name:
            Path(temporary_name).unlink(missing_ok=True)

    print(f"Created: {archive}")
    print(f"Created: {sidecar}")
    print(f"Archive SHA-256: {archive_hash}")
    print("Package inventory:")
    for name in sorted(expected_names):
        print(f"  {name}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
