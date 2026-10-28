#!/usr/bin/env bash
# Backup dei dati serie (ADR-004 §6, finestra di rollback del cutover DB).
# Snapshot SQLite consistente (online backup API, sicuro a demone acceso) +
# checksum + verifica di parità JSON↔DB inclusa nel report.
#
# Uso:
#   scripts/db_backup.sh [destinazione]
#       destinazione default: ~/Anidownloader-backup/<timestamp>
#   ANIDOWNLOADER_CONFIG_HOME=<dir> scripts/db_backup.sh ...
#       per backuppare una configurazione diversa da ~/.config/AniDownloader
#       (drill, sandbox, istanze di test).
set -euo pipefail

CONF="${ANIDOWNLOADER_CONFIG_HOME:-$HOME/.config/AniDownloader}"
JSON="$CONF/series_data.json"
DB="$CONF/series_data.db"
DEST="${1:-$HOME/Anidownloader-backup/$(date +%Y%m%dT%H%M%S)}"

if [ ! -d "$CONF" ]; then
    echo "ERRORE: configurazione non trovata: $CONF" >&2
    exit 1
fi
mkdir -p "$DEST"

cp -a "$CONF/config.json" "$DEST/" 2>/dev/null || true
cp -a "$JSON" "$DEST/" 2>/dev/null || echo "AVVISO: $JSON assente"

# Snapshot del DB con la API di backup SQLite: consistente anche con il demone
# vivo (WAL). I file terzi (-wal/-shm) non vengono copiati: il dump è auto-
# consistente per costruzione.
if [ -f "$DB" ]; then
    python3 - "$DB" "$DEST/series_data.db" <<'PY'
import sqlite3, sys

src, dst = sys.argv[1], sys.argv[2]
sconn = sqlite3.connect(src)
try:
    dconn = sqlite3.connect(dst)
    try:
        sconn.backup(dconn)
    finally:
        dconn.close()
finally:
    sconn.close()
PY
else
    echo "AVVISO: $DB assente (cutover non ancora eseguito?)"
    rm -f "$DEST/series_data.db"
fi

# Checksum di ciò che è in backup (verificato dal restore): solo i file
# realmente presenti, per non abortire con set -e su config/DB mancanti.
: > "$DEST/sha256sums.txt"
for f in series_data.json series_data.db config.json; do
    if [ -f "$DEST/$f" ]; then
        ( cd "$DEST" && sha256sum "$f" ) >> "$DEST/sha256sums.txt"
    fi
done

# Parità conteggio+campi tra l'export JSON e lo snapshot del DB (zip in ordine
# di rowid = ordine array dell'export).
python3 - "$DEST/series_data.json" "$DEST/series_data.db" <<'PY'
import json, sqlite3, sys

json_path, db_path = sys.argv[1], sys.argv[2]
try:
    with open(json_path, encoding="utf-8") as f:
        export = json.load(f)
except Exception as e:  # report umano, non crash: codice distinto per tipo
    print(f"PARITA: export JSON illeggibile: {e}")
    sys.exit(3)
if not isinstance(export, list):
    print("PARITA: export JSON non è un array")
    sys.exit(3)

conn = sqlite3.connect(db_path)
conn.row_factory = sqlite3.Row
try:
    rows = conn.execute(
        "SELECT name, passed_episodes, series_page_url, last_downloaded_episode "
        "FROM series ORDER BY rowid").fetchall()
except Exception as e:
    print(f"PARITA: snapshot DB illeggibile: {e}")
    sys.exit(3)
finally:
    conn.close()

if len(rows) != len(export):
    print(f"PARITA: DIVERGENZA conteggi: esportato {len(export)} vs DB {len(rows)}")
    sys.exit(1)
for line, riga in zip(export, rows):
    campi_json = (line.get("name", ""), line.get("passed_episodes"),
                  line.get("series_page_url"), line.get("last_downloaded_episode"))
    campi_db = (riga["name"], riga["passed_episodes"],
                riga["series_page_url"], riga["last_downloaded_episode"])
    if campi_json != campi_db:
        print(f"PARITA: DIVERGENZA campi (JSON {campi_json} vs DB {campi_db})")
        sys.exit(1)
print(f"PARITA: OK ({len(export)} serie identiche tra export e DB)")
PY

echo "Backup completato: $DEST"
