"""Repository configuration invariants for the uni-usb-dongle v1.2.0 release."""

import configparser
import re
import unittest
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent

WORKFLOW_FILES = ("ci.yml", "nightly.yml", "release.yml")


def _read(rel_path: str) -> str:
    return (REPO_ROOT / rel_path).read_text(encoding="utf-8")


def _parse_jobs(content: str) -> dict:
    """Extract top-level job name -> list of raw body lines from a workflow file."""
    jobs: dict = {}
    in_jobs = False
    current_job = None
    for raw in content.splitlines():
        line = raw.rstrip("\r")
        if re.match(r"^jobs:\s*$", line):
            in_jobs = True
            current_job = None
            continue
        if not in_jobs:
            continue
        if line and not line.startswith(" "):
            in_jobs = False
            current_job = None
            continue
        match = re.match(r"^  ([A-Za-z0-9_.-]+):\s*$", line)
        if match:
            current_job = match.group(1)
            jobs[current_job] = []
            continue
        if current_job is not None:
            jobs[current_job].append(line)
    return jobs


class RepoConfigTests(unittest.TestCase):
    def test_gitmodules_registers_bouffalo_sdk_submodule(self):
        parser = configparser.ConfigParser()
        read_files = parser.read(REPO_ROOT / ".gitmodules", encoding="utf-8")
        self.assertTrue(read_files, ".gitmodules must exist at repo root")

        target_section = None
        for section in parser.sections():
            if parser.get(section, "path", fallback="").strip() == "third_party/bouffalo_sdk":
                target_section = section
                break

        self.assertIsNotNone(
            target_section,
            "third_party/bouffalo_sdk must be registered in .gitmodules",
        )
        self.assertEqual(parser.get(target_section, "path").strip(), "third_party/bouffalo_sdk")
        self.assertEqual(
            parser.get(target_section, "url").strip(),
            "https://github.com/ting1996/bouffalo_sdk.git",
        )
        self.assertEqual(parser.get(target_section, "branch").strip(), "uni-usb-dongle")

    def test_firmware_makefile_defaults_to_submodule_and_checks_missing_sdk(self):
        content = _read("firmware/Makefile")
        self.assertIn(
            "BL_SDK_BASE ?= $(abspath ../third_party/bouffalo_sdk)",
            content,
        )
        include_token = "include $(BL_SDK_BASE)/project.build"
        guard_re = re.compile(
            r"ifeq\s*\(\$\(wildcard\s+[^\n]*project\.build\),\)\s*\n\$\(error\s+[^\n]*third_party/bouffalo_sdk[^\n]*\)\s*\nendif",
            re.MULTILINE,
        )
        self.assertIn(include_token, content)
        guard_match = guard_re.search(content)
        self.assertIsNotNone(
            guard_match,
            "firmware/Makefile must check for missing project.build with an $(error ...) message",
        )
        self.assertLess(
            guard_match.end(),
            content.index(include_token),
            "missing-submodule error guard must appear before include $(BL_SDK_BASE)/project.build",
        )

    def test_cmake_project_version_is_1_2_0(self):
        content = _read("CMakeLists.txt")
        self.assertIn(
            "project(uni_usb_dongle VERSION 1.2.0 LANGUAGES C)",
            content,
        )

    def test_root_license_is_verbatim_gplv3(self):
        license_path = REPO_ROOT / "LICENSE"
        self.assertTrue(license_path.is_file(), "LICENSE must exist at repo root")
        content = license_path.read_text(encoding="utf-8")
        self.assertEqual(len(content.splitlines()), 674)
        for needle in (
            "GNU GENERAL PUBLIC LICENSE",
            "Version 3, 29 June 2007",
            "Copyright (C) 2007 Free Software Foundation, Inc.",
            "TERMS AND CONDITIONS",
            "17. Interpretation of Sections 15 and 16.",
            "END OF TERMS AND CONDITIONS",
        ):
            self.assertIn(needle, content)

    def test_workflows_enable_submodules_only_for_firmware_build(self):
        checkout_with_submodules_re = re.compile(
            r"uses:\s*actions/checkout@v4\s*\n\s+with:\s*\n(?:\s+(?!-)[A-Za-z0-9_-]+:[^\n]*\n)*?\s+submodules:\s*true\b"
        )
        for workflow_name in WORKFLOW_FILES:
            rel = f".github/workflows/{workflow_name}"
            content = _read(rel)
            jobs = _parse_jobs(content)
            self.assertIn("firmware-build", jobs, f"{rel} must define firmware-build")
            for job_name, job_lines in jobs.items():
                body = "\n".join(job_lines)
                has_submodules = bool(re.search(r"(?m)^\s+submodules:\s*true\b", body))
                if job_name == "firmware-build":
                    self.assertRegex(
                        body,
                        checkout_with_submodules_re,
                        f"{rel}: firmware-build must checkout via actions/checkout@v4 with submodules: true",
                    )
                else:
                    self.assertFalse(
                        has_submodules,
                        f"{rel}: job {job_name} must NOT set submodules: true",
                    )

    def test_build_firmware_action_uses_submodule(self):
        content = _read(".github/actions/build-firmware/action.yml")
        self.assertIn(
            "${{ github.workspace }}/third_party/bouffalo_sdk",
            content,
        )
        self.assertIn(
            "git submodule update --init --depth 1 third_party/bouffalo_sdk",
            content,
        )
        self.assertIn('BL_SDK_BASE="${SDK_BASE}"', content)
        for forbidden in (
            "bouffalolab/bouffalo_sdk.git",
            "~/bouffalo_sdk",
            "$HOME/bouffalo_sdk",
        ):
            self.assertNotIn(
                forbidden,
                content,
                f"legacy SDK location must not be referenced: {forbidden}",
            )

    def test_build_firmware_action_uses_bouffalo_toolchain_release(self):
        content = _read(".github/actions/build-firmware/action.yml")
        self.assertNotIn("bouffalolab/toolchain_gcc_t-head_linux", content)
        self.assertIn(
            "https://github.com/bouffalolab/toolchain/releases/download/compact-old/xuantie-gcc-riscv-v2.10.2-linux-x86_64.tar.xz",
            content,
        )
        self.assertIn(
            "a3a6fd5fe9fce64c09fb6d96cb1075ba114ee466d73bba6be5a4462fee978c83",
            content,
        )

    def test_graphifyignore_excludes_third_party(self):
        content = _read(".graphifyignore")
        non_comment = [
            ln.strip()
            for ln in content.splitlines()
            if ln.strip() and not ln.lstrip().startswith("#")
        ]
        self.assertTrue(
            any(entry in ("third_party/", "third_party") for entry in non_comment),
            ".graphifyignore must contain non-comment third_party/ entry",
        )


if __name__ == "__main__":
    unittest.main()
