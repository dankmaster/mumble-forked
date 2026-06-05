# Stonks Live Server Update

This runbook covers a live deploy that includes the current stonks database
tables. It assumes the server is updated by replacing the `mumble-server`
binary and then letting the normal Murmur database migration run at startup.

## What Changes

The server database schema is currently version `22`.

The current stonks ledger schema includes:

- `stonks_scores`
- `stonks_follows`
- `stonks_feed_preferences`
- `stonks_pinned_tickers`
- `stonks_snapshots`
- `stonks_snapshot_positions`

No manual SQL migration is required for a normal update. The server creates
and migrates these tables during startup through the existing database
migration path. The important operational requirement is to keep a backup of
the database files before the first startup with the new binary.

## Before Deploying

Make sure the commit you want to deploy is fetchable from the server-side
clone. The local dank-server deploy wrapper builds on the server from a Git
commit, so purely local unpushed commits are not deployable through that flow.

On Linux servers, identify these before starting:

- the systemd service name, for example `mumble-server` or `murmur`
- the SQLite database path, usually configured in `mumble-server.ini`
- the installed `mumble-server` binary path

Recommended local checks before a live test:

```powershell
.\scripts\local\with-vs-dev-env.ps1 -Run "cmake --build .tmp\stonks-server-build --target mumble-server --parallel 4"
ctest --test-dir build -R "Test(StonksCommand|FinanceQuote)" --output-on-failure
ctest --test-dir .tmp\stonks-server-build -R TestServerDatabase --output-on-failure
git diff --check
```

## Linux Server Deploy

This is the generic Linux flow when updating a server directly. Adjust the
paths for the actual host.

For the reusable migration flow, see
`docs/dev/maintenance/MurmurDatabaseMigration.md`. The stonks update can use
the generic helper:

```bash
bash scripts/migrate-murmur-db.sh \
  --binary /usr/local/bin/mumble-server \
  --ini /etc/mumble-server.ini \
  --service mumble-server \
  --user mumble-server \
  --db /var/lib/mumble-server/mumble-server.sqlite \
  --expected-version 22
```

If the full binary deploy is being done manually, the equivalent low-level
flow is:

```bash
SERVICE=mumble-server
DB=/var/lib/mumble-server/mumble-server.sqlite
NEW_BINARY=/srv/mumble/src/dankmaster-mumble/build/mumble-server
INSTALLED_BINARY=/usr/local/bin/mumble-server
BACKUP_DIR=/var/backups/mumble/stonks-$(date +%Y%m%d-%H%M%S)

sudo systemctl stop "$SERVICE"

sudo install -d -m 700 "$BACKUP_DIR"
for path in "$DB" "$DB"-wal "$DB"-shm "$DB"-journal; do
  if [ -e "$path" ]; then
    sudo cp -a "$path" "$BACKUP_DIR"/
  fi
done

sudo install -m 0755 "$NEW_BINARY" "$INSTALLED_BINARY"
sudo systemctl start "$SERVICE"
```

Then verify the migration:

```bash
bash scripts/check-stonks-db.sh "$DB"
sudo journalctl -u "$SERVICE" -n 80 --no-pager
```

If the service is not managed by systemd, use the equivalent stop/start command
for that host. The important part is that the database backup happens before
the first startup with the schema `22` binary.

## Dank-Server Deploy

The local dank-server wrapper is a convenience layer around a remote Linux
deploy script. It still updates the Linux server-side clone and binary.

Run a dry run first:

```powershell
.\scripts\local\deploy-dank-dev-server.ps1 -DryRun
```

Then deploy:

```powershell
.\scripts\local\deploy-dank-dev-server.ps1
```

For a schema bump, do not pass `-SkipBackup`. The remote deploy script backs up
`mumble-server.sqlite*` from:

```text
/srv/mumble/src/dankmaster-mumble/dev-instance/
```

into:

```text
/srv/mumble/src/dankmaster-mumble/dev-instance/backups/<timestamp>/
```

If the server should deploy a specific published commit instead of local
`HEAD`, pass it explicitly:

```powershell
.\scripts\local\deploy-dank-dev-server.ps1 -Sha <commit-sha>
```

## Verify After Startup

On the server, verify that the schema reached version `22` and that the stonks
tables exist:

```bash
bash scripts/check-stonks-db.sh /path/to/mumble-server.sqlite
```

Or run the SQL directly:

```bash
cd /srv/mumble/src/dankmaster-mumble/dev-instance
sqlite3 mumble-server.sqlite \
  "SELECT meta_value FROM meta WHERE meta_key = 'schema_version';
   SELECT name FROM sqlite_master
   WHERE type = 'table'
     AND name IN (
       'stonks_scores',
       'stonks_follows',
       'stonks_feed_preferences',
       'stonks_pinned_tickers',
       'stonks_snapshots',
       'stonks_snapshot_positions'
     )
   ORDER BY name;"
```

Expected output:

```text
22
stonks_feed_preferences
stonks_follows
stonks_pinned_tickers
stonks_scores
stonks_snapshot_positions
stonks_snapshots
```

If the database file name differs, use the database path configured in the
instance `mumble-server.ini`.

Also check the instance log after startup:

```bash
tail -n 80 /srv/mumble/src/dankmaster-mumble/dev-instance/stdout.log
```

There should be no database migration errors.

## Smoke Test In Client

Use a text channel named `stonks`. The command handler is intentionally scoped
to that room for the first live version.

Try:

```text
rklb
score 30d +4.2
leaderboard 30d
me
```

The first command should produce a quote request, and the score commands should
persist across reconnects and server restarts.

## Rollback

Treat schema `22` as a forward database change. If the new binary must be
rolled back, restore the pre-migration database backup too. Do not run an older
server binary against the already-migrated schema `22` database unless that
older binary also knows schema `22`.

Rollback outline:

1. Stop the dev instance.
2. Restore `mumble-server.sqlite*` from the timestamped backup created before
   the schema `22` startup.
3. Deploy or restore the previous `mumble-server` binary.
4. Start the instance again.
5. Check `stdout.log` and confirm the old binary accepts the restored database.

The backup is the rollback path; there is no down-migration script for removing
the stonks tables.

Linux rollback example:

```bash
SERVICE=mumble-server
DB=/var/lib/mumble-server/mumble-server.sqlite
BACKUP_DIR=/var/backups/mumble/stonks-YYYYMMDD-HHMMSS
OLD_BINARY=/srv/mumble/releases/previous/mumble-server
INSTALLED_BINARY=/usr/local/bin/mumble-server

sudo systemctl stop "$SERVICE"
sudo cp -a "$BACKUP_DIR"/mumble-server.sqlite* "$(dirname "$DB")"/
sudo install -m 0755 "$OLD_BINARY" "$INSTALLED_BINARY"
sudo systemctl start "$SERVICE"
sudo journalctl -u "$SERVICE" -n 80 --no-pager
```
