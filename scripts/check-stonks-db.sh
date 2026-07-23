#!/usr/bin/env bash
set -euo pipefail

usage() {
	cat <<'USAGE'
Usage: scripts/check-stonks-db.sh [--expect-version VERSION] <mumble-server.sqlite>

Verifies, read-only, that a Murmur SQLite database has the Stonks schema migration applied.
Defaults to expecting schema version 24.
USAGE
}

expected_version="24"
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

if [[ ! "$expected_version" =~ ^[0-9]+$ ]]; then
	echo "Schema version must be a non-negative integer: $expected_version" >&2
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

run_sql() {
	sqlite3 -readonly "$db_path" "$1"
}

quick_check="$(run_sql "PRAGMA quick_check;")"
if [[ "$quick_check" != "ok" ]]; then
	echo "SQLite quick_check failed: $quick_check" >&2
	exit 1
fi

schema_version="$(run_sql "SELECT meta_value FROM meta WHERE meta_key = 'schema_version';")"

if [[ "$schema_version" != "$expected_version" ]]; then
	echo "Unexpected schema version: got '$schema_version', expected '$expected_version'." >&2
	exit 1
fi

required_tables=(stonks_follows stonks_scores)
if [[ "$expected_version" -ge 20 ]]; then
	required_tables+=(stonks_snapshots stonks_snapshot_positions)
fi
if [[ "$expected_version" -ge 22 ]]; then
	required_tables+=(stonks_feed_preferences stonks_pinned_tickers)
fi
if [[ "$expected_version" -ge 23 ]]; then
	required_tables+=(stonks_valuations)
fi

missing_tables=()
for table_name in "${required_tables[@]}"; do
	exists="$(run_sql "SELECT COUNT(*) FROM sqlite_master WHERE type = 'table' AND name = '$table_name';")"
	if [[ "$exists" != "1" ]]; then
		missing_tables+=("$table_name")
	fi
done

if [[ ${#missing_tables[@]} -gt 0 ]]; then
	echo "Missing Stonks tables: ${missing_tables[*]}" >&2
	exit 1
fi

missing_columns=()
if [[ "$expected_version" -ge 21 ]]; then
	for column_name in provider_id provider_symbol exchange quote_time quote_source_url quote_confidence; do
		exists="$(run_sql "SELECT COUNT(*) FROM pragma_table_info('stonks_snapshot_positions') WHERE name = '$column_name';")"
		if [[ "$exists" != "1" ]]; then
			missing_columns+=("stonks_snapshot_positions.$column_name")
		fi
	done
fi

if [[ "$expected_version" -ge 23 ]]; then
	for column_name in server_id user_id portfolio_snapshot_id valued_at total_value currency source priced_positions total_positions estimated; do
		exists="$(run_sql "SELECT COUNT(*) FROM pragma_table_info('stonks_valuations') WHERE name = '$column_name';")"
		if [[ "$exists" != "1" ]]; then
			missing_columns+=("stonks_valuations.$column_name")
		fi
	done
fi

if [[ ${#missing_columns[@]} -gt 0 ]]; then
	echo "Missing Stonks columns: ${missing_columns[*]}" >&2
	exit 1
fi

score_count="$(run_sql "SELECT COUNT(*) FROM stonks_scores;")"
follow_count="$(run_sql "SELECT COUNT(*) FROM stonks_follows;")"

echo "OK: SQLite quick_check=$quick_check"
echo "OK: schema_version=$schema_version"
echo "OK: stonks_scores rows=$score_count"
echo "OK: stonks_follows rows=$follow_count"

if [[ "$expected_version" -ge 20 ]]; then
	snapshot_count="$(run_sql "SELECT COUNT(*) FROM stonks_snapshots;")"
	position_count="$(run_sql "SELECT COUNT(*) FROM stonks_snapshot_positions;")"
	echo "OK: stonks_snapshots rows=$snapshot_count"
	echo "OK: stonks_snapshot_positions rows=$position_count"
fi

if [[ "$expected_version" -ge 21 ]]; then
	echo "OK: stonks_snapshot_positions quote metadata columns present"
fi

if [[ "$expected_version" -ge 22 ]]; then
	preference_count="$(run_sql "SELECT COUNT(*) FROM stonks_feed_preferences;")"
	pinned_count="$(run_sql "SELECT COUNT(*) FROM stonks_pinned_tickers;")"
	echo "OK: stonks_feed_preferences rows=$preference_count"
	echo "OK: stonks_pinned_tickers rows=$pinned_count"
fi

if [[ "$expected_version" -ge 23 ]]; then
	valuation_count="$(run_sql "SELECT COUNT(*) FROM stonks_valuations;")"
	valuation_range="$(run_sql "SELECT COALESCE(MIN(valued_at), ''), COALESCE(MAX(valued_at), '') FROM stonks_valuations;")"
	duplicate_keys="$(run_sql "SELECT COUNT(*) FROM (SELECT server_id, user_id, valued_at FROM stonks_valuations GROUP BY server_id, user_id, valued_at HAVING COUNT(*) > 1);")"
	orphaned_snapshots="$(run_sql "SELECT COUNT(*) FROM stonks_valuations AS v LEFT JOIN stonks_snapshots AS s ON s.server_id = v.server_id AND s.snapshot_id = v.portfolio_snapshot_id WHERE s.snapshot_id IS NULL;")"

	if [[ "$duplicate_keys" != "0" ]]; then
		echo "Duplicate Stonks valuation keys found: $duplicate_keys" >&2
		exit 1
	fi
	if [[ "$orphaned_snapshots" != "0" ]]; then
		echo "Orphaned Stonks valuation snapshots found: $orphaned_snapshots" >&2
		exit 1
	fi

	echo "OK: stonks_valuations rows=$valuation_count range=$valuation_range"
	echo "OK: stonks_valuations identity keys are unique"
	echo "OK: stonks_valuations snapshot references are valid"
fi

if [[ "$expected_version" -ge 24 ]]; then
	legacy_superuser_credentials="$(run_sql "SELECT COUNT(*) FROM server_logs WHERE message LIKE 'Initialized ''SuperUser'' password on server % to ''%'';")"
	if [[ "$legacy_superuser_credentials" != "0" ]]; then
		echo "Legacy plaintext SuperUser credentials remain in server_logs: $legacy_superuser_credentials" >&2
		exit 1
	fi
	echo "OK: legacy plaintext SuperUser log credentials removed"
fi
