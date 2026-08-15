#!/usr/bin/env python3
"""Platform-neutral tests for the guarded Windows installer builder."""

from __future__ import annotations

import importlib.util
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest import mock


ROOT = Path(__file__).resolve().parent.parent
MODULE_PATH = ROOT / "tools" / "build_installer.py"
SPEC = importlib.util.spec_from_file_location("kisakcod_build_installer", MODULE_PATH)
if SPEC is None or SPEC.loader is None:
    raise RuntimeError(f"could not load {MODULE_PATH}")
build_installer = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(build_installer)


class InstallerBuilderTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory(
            prefix="kisakcod-installer-builder-test-"
        )
        self.root = Path(self.temporary.name)
        self.payload = self.root / "payload"
        self.output = self.root / "output"
        self.payload.mkdir()
        for name in sorted(build_installer.REQUIRED_PAYLOAD_FILES):
            path = self.payload / Path(name)
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes((name + "\r\n").encode("ascii"))

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def test_inventory_and_manifest_are_deterministic(self) -> None:
        (self.payload / "miles").mkdir(exist_ok=True)
        (self.payload / "miles" / "mssvoice.asi").write_bytes(b"runtime")
        inventory = build_installer.safe_payload_inventory(self.payload)
        self.assertEqual(
            inventory,
            sorted(inventory, key=lambda name: (name.casefold(), name)),
        )
        manifest = build_installer.manifest_bytes(inventory)
        self.assertTrue(manifest.endswith(b"\r\n"))
        self.assertIn(b"miles\\mssvoice.asi\r\n", manifest)
        self.assertNotIn(b"/", manifest)

    def test_installer_metadata_namespace_is_forbidden(self) -> None:
        protected = self.payload / "KisakCOD-VR-Installer" / "receipt.txt"
        protected.parent.mkdir()
        protected.write_bytes(b"unsafe collision")
        with self.assertRaisesRegex(
            build_installer.InstallerBuildError,
            "unsafe installer payload path",
        ):
            build_installer.safe_payload_inventory(self.payload)

    def test_pascal_character_literal_cannot_start_ispp_directive_line(self) -> None:
        script = (
            ROOT / "release" / "installer" / "KisakCOD-VR.iss"
        ).read_text(encoding="utf-8")
        self.assertNotRegex(
            script,
            r"(?m)^[ \t]*#[0-9]",
            "a Pascal # character literal at the start of a physical line "
            "is parsed as an unknown ISPP directive",
        )

    def test_close_applications_registration_supports_inno_6_and_7(self) -> None:
        script = (
            ROOT / "release" / "installer" / "KisakCOD-VR.iss"
        ).read_text(encoding="utf-8")
        self.assertIn("#if VER >= EncodeVer(7, 0, 0)", script)
        self.assertIn(
            "RegisterExtraCloseApplicationsResource(FileName)",
            script,
        )
        self.assertIn(
            "RegisterExtraCloseApplicationsResource(False, FileName)",
            script,
        )
        self.assertEqual(
            script.count("RegisterCloseApplicationsResource("),
            5,
            "the compatibility wrapper must be declared once and used for "
            "all four guarded resources",
        )

    def test_ispp_command_line_values_are_not_source_quoted(self) -> None:
        values = (
            "0.10.0-beta.14-installer-test.1",
            r"C:\KisakCOD Work\payload",
            r"C:\Users\Tester\AppData\Local\Temp\payload-manifest.txt",
            "0123456789abcdef",
            "{{D57A8D64-49E1-41C6-A5B4-3C3076885F8D}",
        )
        for value in values:
            with self.subTest(value=value):
                self.assertEqual(
                    build_installer.ispp_definition_value(value),
                    value,
                )
                self.assertNotIn('"', value)

        for value in ('bad"quote', "bad\0value", "bad\x01value", "bad\rvalue", "bad\nvalue"):
            with self.subTest(value=repr(value)):
                with self.assertRaisesRegex(
                    build_installer.InstallerBuildError,
                    "forbidden",
                ):
                    build_installer.ispp_definition_value(value)

    def test_compiler_output_and_sha256_sidecar_are_verified(self) -> None:
        compiler = self.root / "ISCC.exe"
        compiler.write_bytes(b"test compiler placeholder")
        script = ROOT / "release" / "installer" / "KisakCOD-VR.iss"

        def fake_run(command, **kwargs):
            del kwargs
            self.assertIn(
                "/DMyAppId={{D57A8D64-49E1-41C6-A5B4-3C3076885F8D}",
                command,
            )
            self.assertIn("/DMyVersion=0.10.0-beta.14", command)
            self.assertIn(
                f"/DMyPayloadDir={self.payload.resolve()}",
                command,
            )
            definitions = [
                argument for argument in command if argument.startswith("/DMy")
            ]
            self.assertTrue(
                any(argument.startswith("/DMyManifest=") for argument in definitions)
            )
            self.assertFalse(any('"' in argument for argument in definitions))
            output_directory = Path(
                next(argument[2:] for argument in command if argument.startswith("/O"))
            )
            output_stem = next(
                argument[2:] for argument in command if argument.startswith("/F")
            )
            output_directory.mkdir(parents=True, exist_ok=True)
            (output_directory / f"{output_stem}.exe").write_bytes(
                b"MZ" + (b"\0" * 600_000)
            )
            return subprocess.CompletedProcess(command, 0, "compiled", "")

        with mock.patch.object(
            build_installer.subprocess,
            "run",
            side_effect=fake_run,
        ):
            setup, sidecar, inventory = build_installer.compile_installer(
                version="0.10.0-beta.14",
                payload_directory=self.payload,
                output_directory=self.output,
                compiler=compiler,
                script=script,
                app_id="d57a8d64-49e1-41c6-a5b4-3c3076885f8d",
            )

        self.assertEqual(setup.name, "KisakCOD-VR-v0.10.0-beta.14-Setup.exe")
        self.assertEqual(sidecar.name, setup.name + ".sha256")
        self.assertEqual(len(inventory), len(build_installer.REQUIRED_PAYLOAD_FILES))
        expected_hash = build_installer.sha256_file(setup)
        self.assertEqual(
            sidecar.read_text(encoding="ascii"),
            f"{expected_hash} *{setup.name}\n",
        )

    def test_non_windows_compiler_output_is_rejected(self) -> None:
        compiler = self.root / "ISCC.exe"
        compiler.write_bytes(b"test compiler placeholder")
        script = ROOT / "release" / "installer" / "KisakCOD-VR.iss"

        def fake_run(command, **kwargs):
            del kwargs
            output_directory = Path(
                next(argument[2:] for argument in command if argument.startswith("/O"))
            )
            output_stem = next(
                argument[2:] for argument in command if argument.startswith("/F")
            )
            output_directory.mkdir(parents=True, exist_ok=True)
            (output_directory / f"{output_stem}.exe").write_bytes(
                b"NO" + (b"\0" * 600_000)
            )
            return subprocess.CompletedProcess(command, 0, "compiled", "")

        with mock.patch.object(
            build_installer.subprocess,
            "run",
            side_effect=fake_run,
        ):
            with self.assertRaisesRegex(
                build_installer.InstallerBuildError,
                "not a Windows executable",
            ):
                build_installer.compile_installer(
                    version="0.10.0-beta.14",
                    payload_directory=self.payload,
                    output_directory=self.output,
                    compiler=compiler,
                    script=script,
                )


if __name__ == "__main__":
    unittest.main(verbosity=2)
