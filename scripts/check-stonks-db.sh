#!/usr/bin/env bash
set -euo pipefail

usage() {
	cat <<'USAGE'
Usage: scripts/check-stonks-db.sh [--expect-version VERSION] <mumble-server.sqlite>

Verifies that a Murmur SQLite database has the stonks schema migration applied.
Defaults to expecting schema version 20.
USAGE
}

expected_version="20"
db_path=""

while [[ $# -gt 0 ]]; do
	case "$1" in
		--expect-version)
			if [[ $# -lt 2 ]]; then
				echo "Missing value for --expect-version." >&2
				exit 2
			fi
			expected_version="$2"
			shift 2
			;;
		-h | --help)
			usage
			exit 0
			;;
		-*)
			echo "Unknown option: $1" >&2
			usage >&2
			exit 2
			;;
		*)
			if [[ -n "$db_path" ]]; then
				echo "Unexpected extra argument: $1" >&2
				usage >&2
				exit 2
			fi
			db_path="$1"
			shift
			;;
	esac
done

if [[ -z "$db_path" ]]; then
	usage >&2
	exit 2
fi

if ! command -v sqlite3 >/dev/null 2>&1; then
	echo "sqlite3 is required but was not found in PATH." >&2
	exit 1
fi

if [[ ! -f "$db_path" ]]; then
	echo "Database file not found: $db_path" >&2
	exit 1
fi

schema_version="$(
	sqlite3 "$db_path" "SELECT meta_value FROM meta WHERE meta_key = 'schema_version';"
)"

if [[ "$schema_version" != "$expected_version" ]]; then
	echo "Unexpected schema version: got '$schema_version', expected '$expected_version'." >&2
	exit 1
fi

required_tables=(stonks_follows stonks_scores)
if [[ "$expected_version" -ge 20 ]]; then
	required_tables+=(stonks_snapshots stonks_snapshot_positions)
fi

missing_tables=()
for table_name in "${required_tables[@]}"; do
	exists="$(
		sqlite3 "$db_path" "SELECT COUNT(*) FROM sqlite_master WHERE type = 'table' AND name = '$table_name';"
	)"
	if [[ "$exists" != "1" ]]; then
		missing_tables+=("$table_name")
	fi
done

if [[ ${#missing_tables[@]} -gt 0 ]]; then
	echo "Missing stonks tables: ${missing_tables[*]}" >&2
	exit 1
fi

score_count="$(sqlite3 "$db_path" "SELECT COUNT(*) FROM stonks_scores;")"
follow_count="$(sqlite3 "$db_path" "SELECT COUNT(*) FROM stonks_follows;")"

echo "OK: schema_version=$schema_version"
echo "OK: stonks_scores rows=$score_count"
echo "OK: stonks_follows rows=$follow_count"

if [[ "$expected_version" -ge 20 ]]; then
	snapshot_count="$(sqlite3 "$db_path" "SELECT COUNT(*) FROM stonks_snapshots;")"
	position_count="$(sqlite3 "$db_path" "SELECT COUNT(*) FROM stonks_snapshot_positions;")"
	echo "OK: stonks_snapshots rows=$snapshot_count"
	echo "OK: stonks_snapshot_positions rows=$position_count"
fi
