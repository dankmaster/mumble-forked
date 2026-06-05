# Murmur Database Migration

Status snapshot: 2026-06-05.

This runbook covers offline database migration for Murmur servers. It is meant
for operators who want to update a database before or during a server binary
upgrade without starting the full voice server.

## Supported Range

The migration uses the same C++ database migration path as normal
`mumble-server` startup. That means it supports every schema version that the
current binary supports. At the time of writing, the server migrates schema
versions `6` through `22` to the latest schema.

Older schema versions are intentionally rejected by the server migration layer
and need an intermediate upgrade first.

## Migration Command

`mumble-server` provides a migration-only mode:

```bash
mumble-server --no-detach --db-migrate --ini /etc/mumble-server.ini
```

This reads the normal server configuration, opens the configured database,
applies any pending schema migrations, writes the latest schema version, and
exits. It does not boot listening server instances.

## Linux Helper Script

For Linux hosts, use the wrapper script so the common operational steps are
kept together:

```bash
bash scripts/migrate-murmur-db.sh \
  --binary /usr/local/bin/mumble-server \
  --ini /etc/mumble-server.ini \
  --service mumble-server \
  --user mumble-server \
  --db /var/lib/mumble-server/mumble-server.sqlite
```

The script can:

- stop and restart a systemd service
- back up SQLite database files, including WAL/SHM/journal sidecar files
- run `mumble-server --db-migrate`
- print the SQLite schema version after migration

When validating a known schema bump, pass `--expected-version <version>` to
make the script fail if the migrated database does not land on that schema.

For MySQL or PostgreSQL, take a database-native backup first and omit `--db` if
there is no SQLite file to verify:

```bash
bash scripts/migrate-murmur-db.sh \
  --binary /usr/local/bin/mumble-server \
  --ini /etc/mumble-server.ini \
  --service mumble-server \
  --user mumble-server \
  --no-backup \
  --no-verify
```

## Manual SQLite Flow

If the wrapper script is not available, the manual flow is:

```bash
SERVICE=mumble-server
DB=/var/lib/mumble-server/mumble-server.sqlite
BACKUP_DIR=/var/backups/mumble/db-migrate-$(date +%Y%m%d-%H%M%S)

sudo systemctl stop "$SERVICE"

sudo install -d -m 700 "$BACKUP_DIR"
for path in "$DB" "$DB"-wal "$DB"-shm "$DB"-journal; do
  if [ -e "$path" ]; then
    sudo cp -a "$path" "$BACKUP_DIR"/
  fi
done

sudo -u mumble-server mumble-server --no-detach --db-migrate --ini /etc/mumble-server.ini

sqlite3 "$DB" "SELECT meta_value FROM meta WHERE meta_key = 'schema_version';"

sudo systemctl start "$SERVICE"
```

## Rollback

Database migrations are forward-only. If the new binary has to be rolled back,
restore the database backup from before the migration too.

Do not run an older binary against a database that has already been migrated to
a newer schema unless that older binary explicitly supports that schema.
