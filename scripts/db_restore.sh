#!/usr/bin/env bash
# Ripristina i dati serie da un backup fatto con scripts/db_backup.sh
# (ADR-004 §6). Verifica i checksum del backup, controlla che nessun demone
# stia usando QUELLA cartella di configurazione, salva lo stato attuale come
# copia di sicurezza, poi ripristina DB (+ wal/shm rimossi) e file.
#
# Uso:
#   scripts/db_restore.sh <dir-backup>
#   ANIDOWNLOADER_CONFIG_HOME=<dir> scripts/db_restore.sh <dir-backup>
#       per ripristinare una configurazione diversa da ~/.config/AniDownloader
#       (drill, sandbox, istanze di test).
set -euo pipefail

if [ $# -ne 1 ]; then
    echo "Uso: $0 <dir-backup>" >&2
    exit 2
fi
SRC="$1"
CONF="${ANIDOWNLOADER_CONFIG_HOME:-$HOME/.config/AniDownloader}"

[ -f "$SRC/sha256sums.txt" ] || { echo "ERRORE: $SRC non sembra un backup (sha256sums.txt assente)"; exit 1; }
[ -d "$CONF" ] || mkdir -p "$CONF"

# Niente demoni sulla cartella da ripristinare: un DB ripristinato sotto un
# processo vivo verrebbe riscritto dal suo WAL alla prima scrittura.
DEMONI="$(python3 - "$CONF" <<'PY'
import glob, os, sys

target = os.path.realpath(sys.argv[1])
trovati = []
for pid_dir in glob.glob("/proc/[0-9]*"):
    try:
        with open(f"{pid_dir}/comm") as f:
            comm = f.read().strip()
        if comm not in ("AniDownloader", "anidownloaderd"):
            continue
        env = {}
        with open(f"{pid_dir}/environ", "rb") as f:
            for kv in f.read().split(b"\0"):
                if b"=" in kv:
                    k, v = kv.split(b"=", 1)
                    env[k.decode(errors="replace")] = v.decode(errors="replace")
        xdg = env.get("XDG_CONFIG_HOME") or os.path.join(env.get("HOME", ""), ".config")
        conf_processo = os.path.realpath(os.path.join(xdg, "AniDownloader"))
        trovati.append((pid_dir.split("/")[-1], conf_processo))
    except (OSError, ValueError):
        # processo sparito o non ispezionabile: contati comunque, per prudenza
        trovati.append(("?", pid_dir))
conflitti = [p for p in trovati if p[1] == target]
if conflitti:
    print(" ".join(f"pid={p[0]}" for p in conflitti))
PY
)"
if [ -n "$DEMONI" ]; then
    echo "ERRORE: demone attivo sulla cartella da ripristinare ($DEMONI)." >&2
    echo "Fermarlo prima del restore (systemctl --user stop ...)." >&2
    exit 1
fi

# Il backup deve essere integro PRIMA di toccare qualsiasi cosa.
( cd "$SRC" && sha256sum -c sha256sums.txt --quiet )

# Parità minima (conteggi) nel backup stesso.
python3 - "$SRC/series_data.json" "$SRC/series_data.db" <<'PY'
import json, sqlite3, sys

try:
    with open(sys.argv[1], encoding="utf-8") as f:
        export = json.load(f)
    conn = sqlite3.connect(sys.argv[2])
    n = conn.execute("SELECT COUNT(*) FROM series").fetchone()[0]
    conn.close()
    if n != len(export):
        print(f"ERRORE backup parita': esportato {len(export)} vs DB {n}")
        sys.exit(1)
except FileNotFoundError as e:
    print(f"ERRORE: file di backup mancante: {e}")
    sys.exit(3)
PY

# Copia di sicurezza dello stato attuale: anche il restore è reversibile.
SAFE="$SRC/pre_restore_$(date +%Y%m%dT%H%M%S)"
mkdir -p "$SAFE"
for f in series_data.json series_data.db series_data.db-wal series_data.db-shm config.json; do
    if [ -f "$CONF/$f" ]; then
        cp -a "$CONF/$f" "$SAFE/"
    fi
done
echo "Stato attuale salvato in: $SAFE"

# Ripristino: rimossi wal/shm di scorta, poi i file dal backup.
rm -f "$CONF/series_data.db" "$CONF/series_data.db-wal" "$CONF/series_data.db-shm"
cp -a "$SRC/series_data.db" "$CONF/"
for f in series_data.json config.json; do
    if [ -f "$SRC/$f" ]; then
        cp -a "$SRC/$f" "$CONF/"
    fi
done

# Riconferma: i file sul posto hanno gli stessi checksum del backup.
for f in series_data.db series_data.json config.json; do
    if [ -f "$SRC/$f" ] && grep -q "  $f\$" "$SRC/sha256sums.txt"; then
        attuale="$( cd "$CONF" && sha256sum "$f" )"
        atteso="$( grep "  $f\$" "$SRC/sha256sums.txt" )"
        if [ "$attuale" != "$atteso" ]; then
            echo "ERRORE: checksum di $f dopo il ripristino non corrisponde al backup" >&2
            exit 1
        fi
    fi
done

echo "Ripristino verificato. Ora riavviare il demone e controllare che la lista sia intatta."
echo "Ripristinato in: $CONF"
