#!/usr/bin/env bash
set -euo pipefail

usage() {
	cat <<'USAGE'
Usage:
  scripts/migrate-murmur-db.sh --binary PATH --ini PATH [options]

Migrates a Murmur database by running the configured mumble-server binary with
--db-migrate. This uses the same database migration code path as normal server
startup, but exits immediately after the migration has completed.

Options:
  --binary PATH          mumble-server binary to run. Defaults to mumble-server.
  --ini PATH             mumble-server.ini to read.
  --service NAME         systemd service to stop before migration and start after success.
  --user NAME            run mumble-server as this user during migration.
  --db PATH              SQLite database path to back up and verify.
  --backup-dir PATH      Backup destination. Defaults next to the SQLite database.
  --no-backup            Skip SQLite file backup.
  --no-verify            Skip SQLite schema verification.
  --expected-version N   Optional expected schema version after migration.
  -h, --help             Show this help text.

For MySQL/PostgreSQL, take a database-native backup before running this script.
USAGE
}

binary="${MUMBLE_SERVER:-mumble-server}"
ini_path=""
service_name=""
run_as_user=""
db_path=""
backup_dir=""
backup_enabled=1
verify_enabled=1
expected_version=""

while [[ $# -gt 0 ]]; do
	case "$1" in
		--binary)
			if [[ $# -lt 2 ]]; then
				echo "Missing value for --binary." >&2
				exit 2
			fi
			binary="$2"
			shift 2
			;;
		--ini)
			if [[ $# -lt 2 ]]; then
				echo "Missing value for --ini." >&2
				exit 2
			fi
			ini_path="$2"
			shift 2
			;;
		--service)
			if [[ $# -lt 2 ]]; then
				echo "Missing value for --service." >&2
				exit 2
			fi
			service_name="$2"
			shift 2
			;;
		--user)
			if [[ $# -lt 2 ]]; then
				echo "Missing value for --user." >&2
				exit 2
			fi
			run_as_user="$2"
			shift 2
			;;
		--db)
			if [[ $# -lt 2 ]]; then
				echo "Missing value for --db." >&2
				exit 2
			fi
			db_path="$2"
			shift 2
			;;
		--backup-dir)
			if [[ $# -lt 2 ]]; then
				echo "Missing value for --backup-dir." >&2
				exit 2
			fi
			backup_dir="$2"
			shift 2
			;;
		--no-backup)
			backup_enabled=0
			shift
			;;
		--no-verify)
			verify_enabled=0
			shift
			;;
		--expected-version)
			if [[ $# -lt 2 ]]; then
				echo "Missing value for --expected-version." >&2
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
			echo "Unexpected argument: $1" >&2
			usage >&2
			exit 2
			;;
	esac
done

if [[ -z "$ini_path" ]]; then
	echo "--ini is required so the migration uses the intended server configuration." >&2
	exit 2
fi

if [[ ! -f "$ini_path" ]]; then
	echo "INI file not found: $ini_path" >&2
	exit 1
fi

if [[ "$binary" == */* && ! -x "$binary" ]]; then
	echo "mumble-server binary is not executable: $binary" >&2
	exit 1
fi

run_root() {
	if [[ "${EUID:-$(id -u)}" -eq 0 ]]; then
		"$@"
	else
		sudo "$@"
	fi
}

run_as_service_user() {
	local -a command=("$@")

	if [[ -n "$run_as_user" ]]; then
		if [[ "${EUID:-$(id -u)}" -eq 0 ]] && command -v runuser >/dev/null 2>&1; then
			runuser -u "$run_as_user" -- "${command[@]}"
		else
			sudo -u "$run_as_user" -- "${command[@]}"
		fi
	else
		"${command[@]}"
	fi
}

backup_sqlite_database() {
	if [[ -z "$db_path" ]]; then
		echo "No --db supplied; skipping SQLite file backup."
		echo "For MySQL/PostgreSQL, make sure a database-native backup exists before continuing."
		return
	fi

	if [[ -z "$backup_dir" ]]; then
		backup_dir="$(dirname "$db_path")/backups/murmur-db-migrate-$(date +%Y%m%d-%H%M%S)"
	fi

	echo "Creating SQLite backup in $backup_dir"
	run_root install -d -m 700 "$backup_dir"

	local copied=0
	local path
	for path in "$db_path" "$db_path-wal" "$db_path-shm" "$db_path-journal"; do
		if run_root test -e "$path"; then
			run_root cp -a "$path" "$backup_dir"/
			copied=1
		fi
	done

	if [[ "$copied" -ne 1 ]]; then
		echo "No SQLite database files found for backup at $db_path." >&2
		exit 1
	fi
}

verify_sqlite_database() {
	if [[ -z "$db_path" ]]; then
		echo "No --db supplied; skipping SQLite schema verification."
		return
	fi

	if ! command -v sqlite3 >/dev/null 2>&1; then
		echo "sqlite3 is not available; skipping SQLite schema verification."
		return
	fi

	local schema_version
	schema_version="$(
		run_root sqlite3 "$db_path" "SELECT meta_value FROM meta WHERE meta_key = 'schema_version';"
	)"

	if [[ -n "$expected_version" && "$schema_version" != "$expected_version" ]]; then
		echo "Unexpected schema version: got '$schema_version', expected '$expected_version'." >&2
		exit 1
	fi

	echo "OK: schema_version=$schema_version"
}

if [[ -n "$service_name" ]]; then
	echo "Stopping $service_name"
	run_root systemctl stop "$service_name"
fi

if [[ "$backup_enabled" -eq 1 ]]; then
	backup_sqlite_database
fi

echo "Running database migration"
run_as_service_user "$binary" --no-detach --db-migrate --ini "$ini_path"

if [[ "$verify_enabled" -eq 1 ]]; then
	verify_sqlite_database
fi

if [[ -n "$service_name" ]]; then
	echo "Starting $service_name"
	run_root systemctl start "$service_name"
fi

echo "Migration completed."
