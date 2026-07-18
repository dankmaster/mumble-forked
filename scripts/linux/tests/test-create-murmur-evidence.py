#!/usr/bin/env python3

import hashlib
import json
import os
from pathlib import Path
import stat
import subprocess
import tempfile
import unittest


REPOSITORY_ROOT = Path(__file__).resolve().parents[3]
SCRIPT = REPOSITORY_ROOT / "scripts" / "linux" / "create-murmur-evidence.sh"


def shell_path(path):
    return Path(path).resolve().as_posix()


class LinuxMurmurEvidenceTest(unittest.TestCase):
    def setUp(self):
        self.temporary_directory = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary_directory.name)
        self.repository = self.root / "repository"
        self.repository.mkdir()
        subprocess.run(["git", "init", "-q", str(self.repository)], check=True)
        subprocess.run(
            ["git", "-C", str(self.repository), "config", "user.email", "murmur-evidence@example.invalid"],
            check=True,
        )
        subprocess.run(
            ["git", "-C", str(self.repository), "config", "user.name", "Murmur evidence test"],
            check=True,
        )
        (self.repository / "tracked.txt").write_text("candidate source\n", encoding="utf-8")
        subprocess.run(["git", "-C", str(self.repository), "add", "tracked.txt"], check=True)
        subprocess.run(["git", "-C", str(self.repository), "commit", "-qm", "fixture"], check=True)
        self.candidate_sha = subprocess.check_output(
            ["git", "-C", str(self.repository), "rev-parse", "HEAD"], text=True
        ).strip()

        self.build_directory = self.root / "build"
        self.build_directory.mkdir()
        self.server = self.build_directory / ("mumble-server.exe" if os.name == "nt" else "mumble-server")
        self.server.write_bytes(b"server fixture\n")
        self.server.chmod(self.server.stat().st_mode | stat.S_IXUSR)
        self.cmake_cache = self.build_directory / "CMakeCache.txt"
        self.write_cmake_cache()
        self.output = self.root / "evidence.json"

    def tearDown(self):
        self.temporary_directory.cleanup()

    def write_cmake_cache(self, overrides=None):
        entries = {
            "BUILD_NUMBER:STRING": "42",
            "CMAKE_BUILD_TYPE:STRING": "Release",
            "CMAKE_HOME_DIRECTORY:INTERNAL": shell_path(self.repository),
            "client:BOOL": "OFF",
            "screen-helper:BOOL": "OFF",
            "server:BOOL": "ON",
            "static:BOOL": "ON",
            "tests:BOOL": "ON",
        }
        entries.update(overrides or {})
        self.cmake_cache.write_text(
            "".join(f"{key}={value}\n" for key, value in entries.items()), encoding="utf-8"
        )

    def invoke(self, *test_arguments, candidate_sha=None):
        if candidate_sha is None:
            candidate_sha = self.candidate_sha
        return subprocess.run(
            [
                "bash",
                shell_path(SCRIPT),
                "--server-binary",
                shell_path(self.server),
                "--output",
                shell_path(self.output),
                "--candidate-git-sha",
                candidate_sha,
                "--build-number",
                "42",
                "--configuration",
                "linux-x86_64-test",
                "--cmake-cache",
                shell_path(self.cmake_cache),
                "--repository-root",
                shell_path(self.repository),
                *(shell_path(argument) if isinstance(argument, Path) else argument for argument in test_arguments),
            ],
            capture_output=True,
            text=True,
            check=False,
        )

    def test_writes_murmur_only_manifest_without_tests(self):
        result = self.invoke("--tests-not-run")
        self.assertEqual(result.returncode, 0, result.stderr)

        manifest = json.loads(self.output.read_text(encoding="utf-8"))
        self.assertEqual(manifest["candidate_git_sha"], self.candidate_sha)
        self.assertEqual(manifest["build_number"], 42)
        self.assertEqual(manifest["configuration"], "linux-x86_64-test")
        self.assertFalse(manifest["client"])
        self.assertTrue(manifest["server"])
        self.assertEqual(manifest["server_sha256"], hashlib.sha256(self.server.read_bytes()).hexdigest())
        self.assertEqual(
            manifest["cmake_cache_sha256"], hashlib.sha256(self.cmake_cache.read_bytes()).hexdigest()
        )
        self.assertEqual(
            manifest["build_contract"],
            {
                "build_type": "Release",
                "client": False,
                "screen_helper": False,
                "server": True,
                "static": True,
                "tests": True,
            },
        )
        self.assertEqual(manifest["tests"]["status"], "not-run")
        self.assertEqual(manifest["tests"]["total"], 0)

    def test_records_ctest_junit_results(self):
        junit = self.root / "ctest.xml"
        junit.write_text(
            '<testsuite tests="25" failures="0" errors="0" skipped="1"></testsuite>',
            encoding="utf-8",
        )

        result = self.invoke("--test-results", junit)
        self.assertEqual(result.returncode, 0, result.stderr)

        tests = json.loads(self.output.read_text(encoding="utf-8"))["tests"]
        self.assertEqual(tests["status"], "passed")
        self.assertEqual(tests["total"], 25)
        self.assertEqual(tests["skipped"], 1)
        self.assertEqual(tests["result_file"], "ctest.xml")
        self.assertEqual(tests["result_sha256"], hashlib.sha256(junit.read_bytes()).hexdigest())

    def test_rejects_client_or_screen_helper_binaries(self):
        for forbidden_name in ("mumble", "mumble-screen-helper"):
            with self.subTest(forbidden_name=forbidden_name):
                forbidden = self.build_directory / forbidden_name
                forbidden.write_bytes(b"forbidden\n")
                result = self.invoke("--tests-not-run")
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(forbidden_name, result.stderr)
                forbidden.unlink()

    def test_rejects_a_different_candidate_sha(self):
        result = self.invoke("--tests-not-run", candidate_sha="0" * 40)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("does not match the checked-out source", result.stderr)

    def test_rejects_dirty_tracked_candidate_source(self):
        (self.repository / "tracked.txt").write_text("dirty source\n", encoding="utf-8")
        result = self.invoke("--tests-not-run")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("tracked worktree changes", result.stderr)

    def test_rejects_non_server_only_static_test_cache(self):
        invalid_contracts = {
            "client:BOOL": "ON",
            "server:BOOL": "OFF",
            "tests:BOOL": "OFF",
            "screen-helper:BOOL": "ON",
            "static:BOOL": "OFF",
            "CMAKE_BUILD_TYPE:STRING": "Debug",
            "BUILD_NUMBER:STRING": "41",
        }
        for cache_key, invalid_value in invalid_contracts.items():
            with self.subTest(cache_key=cache_key):
                self.write_cmake_cache({cache_key: invalid_value})
                result = self.invoke("--tests-not-run")
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("CMake cache violates", result.stderr)
        self.write_cmake_cache()

    def test_rejects_duplicate_contract_or_source_cache_entries(self):
        with self.cmake_cache.open("a", encoding="utf-8") as cache:
            cache.write("client:BOOL=ON\n")
        result = self.invoke("--tests-not-run")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("CMake cache violates", result.stderr)

        self.write_cmake_cache()
        with self.cmake_cache.open("a", encoding="utf-8") as cache:
            cache.write(f"CMAKE_HOME_DIRECTORY:INTERNAL={shell_path(self.repository)}\n")
        result = self.invoke("--tests-not-run")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("exactly one CMAKE_HOME_DIRECTORY", result.stderr)

    def test_rejects_cache_from_another_source_or_build_directory(self):
        other_source = self.root / "other-source"
        other_source.mkdir()
        self.write_cmake_cache({"CMAKE_HOME_DIRECTORY:INTERNAL": shell_path(other_source)})
        result = self.invoke("--tests-not-run")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("source root does not match", result.stderr)

        self.write_cmake_cache()
        other_cache = self.root / "CMakeCache.txt"
        other_cache.write_bytes(self.cmake_cache.read_bytes())
        original_cache = self.cmake_cache
        self.cmake_cache = other_cache
        try:
            result = self.invoke("--tests-not-run")
        finally:
            self.cmake_cache = original_cache
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("same build directory", result.stderr)

    def test_rejects_failing_junit_results(self):
        junit = self.root / "ctest.xml"
        junit.write_text(
            '<testsuite tests="2" failures="1" errors="0" skipped="0"></testsuite>',
            encoding="utf-8",
        )

        result = self.invoke("--test-results", junit)
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(json.loads(self.output.read_text(encoding="utf-8"))["tests"]["status"], "failed")


if __name__ == "__main__":
    unittest.main()
