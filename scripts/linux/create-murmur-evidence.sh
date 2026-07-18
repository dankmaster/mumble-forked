#!/usr/bin/env bash

set -euo pipefail

usage() {
	cat <<'EOF'
Usage: scripts/linux/create-murmur-evidence.sh \
  --server-binary PATH \
  --output PATH \
  --candidate-git-sha SHA \
  --build-number NUMBER \
  --configuration NAME \
  --cmake-cache PATH \
  (--tests-not-run | --test-results CTEST_JUNIT_XML) \
  [--repository-root PATH]

Validates a Linux Murmur-only binary set and writes a provenance manifest.
EOF
}

server_binary=""
output_path=""
candidate_git_sha=""
build_number=""
configuration=""
cmake_cache=""
test_results=""
tests_not_run=0
repository_root=""

while [[ $# -gt 0 ]]; do
	case "$1" in
		--server-binary)
			server_binary=${2:-}
			shift 2
			;;
		--output)
			output_path=${2:-}
			shift 2
			;;
		--candidate-git-sha)
			candidate_git_sha=${2:-}
			shift 2
			;;
		--build-number)
			build_number=${2:-}
			shift 2
			;;
		--configuration)
			configuration=${2:-}
			shift 2
			;;
		--cmake-cache)
			cmake_cache=${2:-}
			shift 2
			;;
		--test-results)
			test_results=${2:-}
			shift 2
			;;
		--tests-not-run)
			tests_not_run=1
			shift
			;;
		--repository-root)
			repository_root=${2:-}
			shift 2
			;;
		-h|--help)
			usage
			exit 0
			;;
		*)
			echo "Unknown argument: $1" >&2
			usage >&2
			exit 2
			;;
	esac
done

if [[ -z "$server_binary" || -z "$output_path" || -z "$candidate_git_sha" || -z "$build_number" || -z "$configuration" || -z "$cmake_cache" ]]; then
	echo "Missing a required argument." >&2
	usage >&2
	exit 2
fi

if [[ "$tests_not_run" -eq 1 && -n "$test_results" ]] || [[ "$tests_not_run" -eq 0 && -z "$test_results" ]]; then
	echo "Select exactly one of --tests-not-run and --test-results." >&2
	exit 2
fi

if [[ ! "$candidate_git_sha" =~ ^[0-9a-fA-F]{40}$ ]]; then
	echo "Candidate Git SHA must contain exactly 40 hexadecimal characters." >&2
	exit 2
fi
candidate_git_sha=${candidate_git_sha,,}

if [[ ! "$build_number" =~ ^[0-9]+$ ]]; then
	echo "Build number must be a non-negative integer." >&2
	exit 2
fi

if [[ ! -f "$server_binary" || ! -x "$server_binary" ]]; then
	echo "Murmur server binary is missing or not executable: $server_binary" >&2
	exit 1
fi

if [[ ! -f "$cmake_cache" ]]; then
	echo "CMake cache is missing: $cmake_cache" >&2
	exit 1
fi

binary_directory=$(cd "$(dirname "$server_binary")" && pwd -P)
cache_directory=$(cd "$(dirname "$cmake_cache")" && pwd -P)
if [[ "$binary_directory" != "$cache_directory" ]]; then
	echo "Murmur server binary and CMake cache must come from the same build directory." >&2
	exit 1
fi
for forbidden_binary in mumble mumble-screen-helper; do
	if [[ -e "$binary_directory/$forbidden_binary" ]]; then
		echo "Linux Murmur-only build unexpectedly contains $forbidden_binary" >&2
		exit 1
	fi
done

if [[ -z "$repository_root" ]]; then
	repository_root=$(git -C "$(dirname "${BASH_SOURCE[0]}")/../.." rev-parse --show-toplevel)
fi
actual_git_sha=$(git -C "$repository_root" rev-parse HEAD)
actual_git_sha=${actual_git_sha,,}
if [[ "$actual_git_sha" != "$candidate_git_sha" ]]; then
	echo "Candidate Git SHA does not match the checked-out source: expected $candidate_git_sha, got $actual_git_sha" >&2
	exit 1
fi

dirty_tracked=$(git -C "$repository_root" status --porcelain=v1 --untracked-files=no --ignore-submodules=none)
if [[ -n "$dirty_tracked" ]]; then
	echo "Candidate source contains tracked worktree changes; refusing Linux release evidence." >&2
	printf '%s\n' "$dirty_tracked" >&2
	exit 1
fi

declare -A required_cache_entries=(
	[client]="client:BOOL=OFF"
	[server]="server:BOOL=ON"
	[tests]="tests:BOOL=ON"
	[screen-helper]="screen-helper:BOOL=OFF"
	[static]="static:BOOL=ON"
	[build-type]="CMAKE_BUILD_TYPE:STRING=Release"
	[build-number]="BUILD_NUMBER:STRING=$build_number"
)
for contract_name in "${!required_cache_entries[@]}"; do
	contract_entry=${required_cache_entries[$contract_name]}
	cache_key=${contract_entry%%:*}
	mapfile -t cache_matches < <(grep -E "^${cache_key}:[^=]+=" "$cmake_cache" || true)
	if [[ "${#cache_matches[@]}" -ne 1 || "${cache_matches[0]-}" != "$contract_entry" ]]; then
		echo "CMake cache violates the Linux Murmur release contract for $contract_name: expected '$contract_entry'" >&2
		exit 1
	fi
done

mapfile -t cache_source_entries < <(grep -E '^CMAKE_HOME_DIRECTORY:INTERNAL=' "$cmake_cache" || true)
if [[ "${#cache_source_entries[@]}" -ne 1 ]]; then
	echo "CMake cache must record exactly one CMAKE_HOME_DIRECTORY." >&2
	exit 1
fi
cache_source_root=${cache_source_entries[0]#*=}
cache_source_root=$(cd "$cache_source_root" && pwd -P)
repository_root=$(cd "$repository_root" && pwd -P)
if [[ "$cache_source_root" != "$repository_root" ]]; then
	echo "CMake cache source root does not match the candidate repository." >&2
	exit 1
fi

if [[ -n "$test_results" && ! -f "$test_results" ]]; then
	echo "CTest JUnit result file does not exist: $test_results" >&2
	exit 1
fi

mkdir -p "$(dirname "$output_path")"

python_command=""
for candidate in python3 python; do
	if command -v "$candidate" >/dev/null 2>&1 && "$candidate" -c 'import sys; raise SystemExit(0 if sys.version_info >= (3, 8) else 1)' >/dev/null 2>&1; then
		python_command=$candidate
		break
	fi
done
if [[ -z "$python_command" ]]; then
	echo "Python 3.8 or newer is required to write the evidence manifest." >&2
	exit 1
fi

"$python_command" - "$server_binary" "$output_path" "$candidate_git_sha" "$build_number" "$configuration" "$cmake_cache" "$tests_not_run" "$test_results" <<'PY'
import hashlib
import json
import os
import sys
import xml.etree.ElementTree as ET

(
    server_binary,
    output_path,
    candidate_git_sha,
    build_number,
    configuration,
    cmake_cache,
    tests_not_run,
    test_results,
) = sys.argv[1:]


def sha256_file(path):
    digest = hashlib.sha256()
    with open(path, "rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


tests = {"status": "not-run", "total": 0, "failures": 0, "errors": 0, "skipped": 0}
tests_failed = False
if tests_not_run != "1":
    root = ET.parse(test_results).getroot()

    def integer_attribute(name):
        raw_value = root.attrib.get(name)
        if raw_value is not None:
            return int(raw_value)
        return sum(int(suite.attrib.get(name, "0")) for suite in root.findall(".//testsuite"))

    tests.update(
        {
            "total": integer_attribute("tests"),
            "failures": integer_attribute("failures"),
            "errors": integer_attribute("errors"),
            "skipped": integer_attribute("skipped"),
            "result_file": os.path.basename(test_results),
            "result_sha256": sha256_file(test_results),
        }
    )
    tests_failed = tests["failures"] > 0 or tests["errors"] > 0
    tests["status"] = "failed" if tests_failed else "passed"

manifest = {
    "schema_version": 1,
    "candidate_git_sha": candidate_git_sha,
    "build_number": int(build_number),
    "server_sha256": sha256_file(server_binary),
    "cmake_cache_sha256": sha256_file(cmake_cache),
    "configuration": configuration,
    "client": False,
    "server": True,
    "build_contract": {
        "build_type": "Release",
        "client": False,
        "screen_helper": False,
        "server": True,
        "static": True,
        "tests": True,
    },
    "tests": tests,
}

with open(output_path, "w", encoding="utf-8", newline="\n") as handle:
    json.dump(manifest, handle, indent=2, sort_keys=True)
    handle.write("\n")

if tests_failed:
    raise SystemExit("CTest JUnit input contains failing or errored tests")
PY

echo "Wrote Linux Murmur evidence manifest: $output_path"
