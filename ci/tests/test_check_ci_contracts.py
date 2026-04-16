from __future__ import annotations

import tempfile
import unittest
from pathlib import Path

from ci import check_ci_contracts


class SnippetHelperTests(unittest.TestCase):
    def test_check_contains_reports_missing_snippet(self) -> None:
        with tempfile.TemporaryDirectory() as tmp_dir:
            fixture = Path(tmp_dir) / "fixture.txt"
            fixture.write_text("alpha beta", encoding="utf-8")
            errors: list[str] = []

            check_ci_contracts.check_contains(fixture, "gamma", errors)

            self.assertEqual(len(errors), 1)
            self.assertIn("missing required snippet", errors[0])

    def test_check_contains_ignores_present_snippet(self) -> None:
        with tempfile.TemporaryDirectory() as tmp_dir:
            fixture = Path(tmp_dir) / "fixture.txt"
            fixture.write_text("alpha beta gamma", encoding="utf-8")
            errors: list[str] = []

            check_ci_contracts.check_contains(fixture, "beta", errors)

            self.assertEqual(errors, [])

    def test_check_count_at_least_reports_under_minimum(self) -> None:
        with tempfile.TemporaryDirectory() as tmp_dir:
            fixture = Path(tmp_dir) / "fixture.txt"
            fixture.write_text("token\ntoken\n", encoding="utf-8")
            errors: list[str] = []

            check_ci_contracts.check_count_at_least(fixture, "token", 3, errors)

            self.assertEqual(len(errors), 1)
            self.assertIn("at least 3 times, found 2", errors[0])

    def test_check_count_at_least_accepts_exact_minimum(self) -> None:
        with tempfile.TemporaryDirectory() as tmp_dir:
            fixture = Path(tmp_dir) / "fixture.txt"
            fixture.write_text("token\ntoken\ntoken\n", encoding="utf-8")
            errors: list[str] = []

            check_ci_contracts.check_count_at_least(fixture, "token", 3, errors)

            self.assertEqual(errors, [])


class ContractCoverageTests(unittest.TestCase):
    def test_windows_contract_covers_packaging_trigger_and_launch_check(self) -> None:
        expected = {
            "workflow_dispatch:",
            "workflow_call:",
            "tbc-video-export.exe --dump-default-config",
        }
        self.assertTrue(expected.issubset(set(check_ci_contracts.WINDOWS_REQUIRED_SNIPPETS)))

    def test_macos_contract_covers_packaging_trigger_and_launch_check(self) -> None:
        expected = {
            "workflow_dispatch:",
            "workflow_call:",
            "LDDECODE_NNTRANSFORM3D_PROVIDER: cpu",
            "result/share/tbc-video-export",
            "tbc-tools.app/Contents/MacOS/tbc-video-export --version",
        }
        self.assertTrue(expected.issubset(set(check_ci_contracts.MACOS_REQUIRED_SNIPPETS)))

    def test_release_contract_wires_all_platform_packaging_workflows(self) -> None:
        expected = {
            "uses: ./.github/workflows/build_linux_tools.yml",
            "uses: ./.github/workflows/build_windows_tools.yml",
            "uses: ./.github/workflows/build_macos_tools.yml",
        }
        self.assertTrue(expected.issubset(set(check_ci_contracts.RELEASE_REQUIRED_SNIPPETS)))

    def test_bundle_verifier_contract_includes_found_and_launch_checks(self) -> None:
        expected = {
            'run_smoke_test "x86-appimage-extract-and-run-tbc-video-export"',
            'run_smoke_test "x86-appimage-apprun-tbc-video-export"',
            'run_smoke_test "arm64-launcher-tbc-video-export"',
            'require_path "$ROOT/usr/bin/tbc-video-export"',
            'require_path "$TARGET/bin/tbc-video-export"',
        }
        self.assertTrue(expected.issubset(set(check_ci_contracts.BUNDLE_VERIFY_REQUIRED_SNIPPETS)))


if __name__ == "__main__":
    unittest.main()
