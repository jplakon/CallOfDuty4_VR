#!/usr/bin/env python3
"""Compile the guarded KisakCOD VR Windows installer from a staged payload."""

from __future__ import annotations

import argparse
import hashlib
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import NoReturn, Sequence


VERSION_RE = re.compile(r"^\d+\.\d+\.\d+(?:-[0-9A-Za-z][0-9A-Za-z.-]*)?$")
APP_ID_RE = re.compile(
    r"^[0-9A-Fa-f]{8}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-"
    r"[0-9A-Fa-f]{4}-[0-9A-Fa-f]{12}$"
)
REQUIRED_PAYLOAD_FILES = {
    "KisakCOD-sp.exe",
    "KisakCOD-VR-Configurator.exe",
    "KisakCOD-VR-Input-Mapper.exe",
    "Launch-KisakCOD-VR.bat",
    "Launch-KisakCOD-VR-Diagnostics.bat",
    "VR-Settings.bat",
    "README-FIRST.txt",
    "INSTALL.txt",
    "LICENSE-GPLv3.txt",
    "SHA256SUMS.txt",
    "SOURCE.txt",
    "steam_api.dll",
    "binkw32.dll",
    "mss32.dll",
    "miles/mssvoice.asi",
}
INSTALLER_DATA_DIRECTORY = "kisakcod-vr-installer"


class InstallerBuildError(RuntimeError):
    """A release-safe installer build failure."""


def fail(message: str) -> NoReturn:
    raise InstallerBuildError(message)


def sha256_bytes(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def safe_payload_inventory(payload_directory: Path) -> list[str]:
    if not payload_directory.is_dir():
        fail(f"payload directory does not exist: {payload_directory}")

    inventory: list[str] = []
    casefolded: dict[str, str] = {}
    for path in payload_directory.rglob("*"):
        if path.is_symlink():
            fail(f"payload symlinks are forbidden: {path}")
        if not path.is_file():
            continue

        relative = path.relative_to(payload_directory).as_posix()
        parts = Path(relative).parts
        if (
            not parts
            or any(part in ("", ".", "..") for part in parts)
            or any(":" in part for part in parts)
            or parts[0].casefold() == INSTALLER_DATA_DIRECTORY
        ):
            fail(f"unsafe installer payload path: {relative}")

        folded = relative.casefold()
        previous = casefolded.get(folded)
        if previous is not None:
            fail(
                "case-insensitive payload collision: "
                f"{previous!r} and {relative!r}"
            )
        casefolded[folded] = relative
        inventory.append(relative)

    missing = sorted(REQUIRED_PAYLOAD_FILES - set(inventory))
    if missing:
        fail("installer payload is missing: " + ", ".join(missing))
    # WindowsPath ordering is case-insensitive while PosixPath ordering is
    # case-sensitive. The manifest must not depend on the build host, so sort
    # the already collision-checked relative names by one explicit rule.
    inventory.sort(key=lambda name: (name.casefold(), name))
    return inventory


def manifest_bytes(inventory: Sequence[str]) -> bytes:
    windows_names = [name.replace("/", "\\") for name in inventory]
    return ("\r\n".join(windows_names) + "\r\n").encode("utf-8")


def _candidate_compiler(value: str | Path | None) -> Path | None:
    if value is None or str(value).strip() == "":
        return None
    raw = str(value).strip().strip('"')
    candidate = Path(raw)
    if candidate.is_file():
        return candidate.resolve()
    discovered = shutil.which(raw)
    if discovered:
        return Path(discovered).resolve()
    return None


def find_iscc(explicit: str | Path | None = None) -> Path:
    attempted: list[str] = []
    for value in (
        explicit,
        os.environ.get("INNO_SETUP_COMPILER"),
        "ISCC.exe",
        "iscc",
    ):
        if value:
            attempted.append(str(value))
            candidate = _candidate_compiler(value)
            if candidate is not None:
                return candidate

    for environment_name in ("ProgramFiles(x86)", "ProgramFiles"):
        base = os.environ.get(environment_name)
        if not base:
            continue
        for directory_name in ("Inno Setup 7", "Inno Setup 6"):
            candidate_path = Path(base) / directory_name / "ISCC.exe"
            attempted.append(str(candidate_path))
            if candidate_path.is_file():
                return candidate_path.resolve()

    detail = "\n  ".join(attempted) if attempted else "(no candidates)"
    fail(
        "Inno Setup compiler ISCC.exe was not found. Install Inno Setup 6 or "
        "7, pass --iscc, or set INNO_SETUP_COMPILER. Checked:\n  " + detail
    )


def ispp_definition_value(value: str) -> str:
    """Return a raw value suitable for an ISCC /Dname=value argument.

    ISCC stores the right-hand side of a command-line definition as an ISPP
    string already. Adding source-language quote characters here would make
    those quotes part of the value and corrupt absolute Windows filenames.
    """
    if any(character in value for character in ("\x00", "\x01", "\r", "\n")):
        fail("ISPP definition contains a forbidden control character")
    if '"' in value:
        fail("ISPP definition contains a forbidden quote character")
    return value


def compile_installer(
    *,
    version: str,
    payload_directory: Path,
    output_directory: Path,
    compiler: Path,
    script: Path,
    app_id: str | None = None,
) -> tuple[Path, Path, list[str]]:
    if not VERSION_RE.fullmatch(version):
        fail("version must look like 0.10.0-beta.14 and must not start with v")
    if app_id is not None and not APP_ID_RE.fullmatch(app_id):
        fail("app id must be a canonical GUID without braces")
    payload_directory = payload_directory.resolve()
    output_directory = output_directory.resolve()
    script = script.resolve()
    compiler = compiler.resolve()

    if not script.is_file():
        fail(f"installer script is missing: {script}")
    if not compiler.is_file():
        fail(f"Inno Setup compiler is missing: {compiler}")

    inventory = safe_payload_inventory(payload_directory)
    manifest = manifest_bytes(inventory)
    manifest_hash = sha256_bytes(manifest)
    output_directory.mkdir(parents=True, exist_ok=True)

    output_name = f"KisakCOD-VR-v{version}-Setup.exe"
    output_path = output_directory / output_name
    sidecar_path = output_path.with_suffix(output_path.suffix + ".sha256")
    if output_path.exists() or sidecar_path.exists():
        fail(f"installer output already exists: {output_path}")

    with tempfile.TemporaryDirectory(prefix="kisakcod-installer-manifest-") as temp:
        manifest_path = Path(temp) / "payload-manifest.txt"
        manifest_path.write_bytes(manifest)

        command = [
            str(compiler),
            "/Qp",
            f"/O{output_directory}",
            f"/F{output_path.stem}",
            f"/DMyVersion={ispp_definition_value(version)}",
            f"/DMyPayloadDir={ispp_definition_value(str(payload_directory))}",
            f"/DMyManifest={ispp_definition_value(str(manifest_path))}",
            f"/DMyManifestSha256={ispp_definition_value(manifest_hash)}",
            f"/DMyPayloadCount={len(inventory)}",
        ]
        if app_id is not None:
            # Inno Setup treats a single opening brace as the start of a
            # constant. Doubling only that brace emits a literal GUID AppId.
            command.append(
                "/DMyAppId="
                + ispp_definition_value("{{" + app_id.upper() + "}")
            )
        command.append(str(script))
        result = subprocess.run(
            command,
            cwd=script.parent,
            check=False,
            capture_output=True,
            text=True,
        )
        if result.returncode != 0:
            detail = (result.stdout + "\n" + result.stderr).strip()
            fail(
                f"ISCC failed with exit code {result.returncode}"
                + (f":\n{detail}" if detail else "")
            )

    if not output_path.is_file():
        fail(f"ISCC reported success but did not create: {output_path}")
    if output_path.stat().st_size < 512_000:
        fail(f"compiled installer looks unexpectedly small: {output_path}")
    with output_path.open("rb") as executable:
        if executable.read(2) != b"MZ":
            fail(f"compiled installer is not a Windows executable: {output_path}")

    installer_hash = sha256_file(output_path)
    sidecar_path.write_text(
        f"{installer_hash} *{output_path.name}\n",
        encoding="ascii",
        newline="\n",
    )
    return output_path, sidecar_path, inventory


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        description=(
            "Compile a guarded KisakCOD VR Setup.exe from an already staged "
            "release payload."
        )
    )
    parser.add_argument("--version", required=True)
    parser.add_argument("--payload-directory", required=True)
    parser.add_argument("--output-directory", required=True)
    parser.add_argument(
        "--iscc",
        help=(
            "Path to ISCC.exe. If omitted, INNO_SETUP_COMPILER, PATH, and "
            "standard Inno Setup 6/7 locations are checked."
        ),
    )
    parser.add_argument(
        "--validate-only",
        action="store_true",
        help="Validate the payload and print its manifest without running ISCC.",
    )
    parser.add_argument(
        "--app-id",
        help=argparse.SUPPRESS,
    )
    args = parser.parse_args(argv)

    root = Path(__file__).resolve().parent.parent
    payload_directory = Path(args.payload_directory).resolve()
    output_directory = Path(args.output_directory).resolve()
    script = root / "release" / "installer" / "KisakCOD-VR.iss"

    try:
        if not VERSION_RE.fullmatch(args.version):
            fail(
                "version must look like 0.10.0-beta.14 and must not start "
                "with v"
            )
        inventory = safe_payload_inventory(payload_directory)
        if args.validate_only:
            manifest = manifest_bytes(inventory)
            print(f"Payload files: {len(inventory)}")
            print(f"Manifest SHA-256: {sha256_bytes(manifest)}")
            for name in inventory:
                print(f"  {name}")
            return 0

        compiler = find_iscc(args.iscc)
        output, sidecar, inventory = compile_installer(
            version=args.version,
            payload_directory=payload_directory,
            output_directory=output_directory,
            compiler=compiler,
            script=script,
            app_id=args.app_id,
        )
    except InstallerBuildError as error:
        print(f"ERROR: {error}", file=sys.stderr)
        return 1

    print(f"Created: {output}")
    print(f"Created: {sidecar}")
    print(f"Installer SHA-256: {sha256_file(output)}")
    print(f"Guarded payload files: {len(inventory)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
