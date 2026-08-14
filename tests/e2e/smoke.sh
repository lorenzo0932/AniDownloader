#!/usr/bin/env bash
# Smoke test E2E offline per AniDownloader: copre CLI + API web senza rete.
#
# Uso:
#   tests/e2e/smoke.sh                     # usa build/AniDownloader
#   ANIDOWNLOADER_BIN=/path/anidownloaderd tests/e2e/smoke.sh
#   ctest --test-dir build -R smoke_e2e     # via CTest
#
# Richiede: curl, jq, sha256sum, python3 (per il "slow server" che blocca il
# planning durante i test di SIGINT e di download concorrente).
# Opzionali: ffmpeg (per media H265 realistici; senza ffmpeg usa file sparsi).
#
# Tutto gira in un sandbox XDG (~/tmp anidl_smoke_*): ~/.config/AniDownloader
# reale non viene mai toccato (verificato a fine esecuzione).
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO_DIR=$(cd "$SCRIPT_DIR/../.." && pwd)
BIN=${ANIDOWNLOADER_BIN:-"$REPO_DIR/build/AniDownloader"}

PASS=0
FAILS=0

pass() { PASS=$((PASS + 1)); printf '  PASS: %s\n' "$1"; }
fail() { FAILS=$((FAILS + 1)); printf '  FAIL: %s\n' "$1"; }

file_sha() { sha256sum "$1" 2>/dev/null | cut -d' ' -f1; }
http_code() { curl -s -o /dev/null -m 10 -w '%{http_code}' "$@"; }

# ─────────────────────────── PREPARAZIONE ───────────────────────────
for tool in curl jq sha256sum python3; do
    if ! command -v "$tool" >/dev/null 2>&1; then
        echo "ERRORE: $tool non trovato (necessario per smoke.sh)" >&2
        exit 2
    fi
done
if [ ! -x "$BIN" ]; then
    echo "ERRORE: binario non trovato: $BIN (compila prima o imposta ANIDOWNLOADER_BIN)" >&2
    exit 2
fi

RPT=$(mktemp -d /tmp/anidl_smoke_XXXXXX)
CLI_SBX="$RPT/cli"
WEB_SBX="$RPT/web"
mkdir -p "$CLI_SBX/AniDownloader" "$CLI_SBX/cache" "$CLI_SBX/media/SerieA" "$CLI_SBX/media/SerieB"
mkdir -p "$WEB_SBX/AniDownloader" "$WEB_SBX/cache" "$WEB_SBX/media/SerieA" "$WEB_SBX/media/SerieB"

SLOW_PORT=""
SLOW_PID=""
WEB_PID=""
SSE_PID=""

# Stato della config reale: i test non devono mai toccarla (sandbox XDG).
REAL_CFG="$HOME/.config/AniDownloader"
if [ -e "$REAL_CFG" ]; then
    SHA_REAL_BEFORE=$(tar -C "$HOME/.config" -cf - AniDownloader 2>/dev/null | sha256sum | cut -d' ' -f1)
else
    SHA_REAL_BEFORE=""
fi

cleanup() {
    if [ -n "$WEB_PID" ]; then
        kill "$WEB_PID" 2>/dev/null || true
        local i
        for i in $(seq 1 25); do
            kill -0 "$WEB_PID" 2>/dev/null || break
            sleep 0.2
        done
        kill -0 "$WEB_PID" 2>/dev/null && kill -9 "$WEB_PID" 2>/dev/null || true
    fi
    [ -n "$SSE_PID" ] && kill "$SSE_PID" 2>/dev/null || true
    [ -n "$SLOW_PID" ] && kill "$SLOW_PID" 2>/dev/null || true
}
trap cleanup EXIT

# Media finti: se ffmpeg+libx265 ci sono generiamo video H265 validi (>1MB con
# padding), altrimenti file sparsi. Con video H265 validi il planning della
# conversione locale non scatta (codec già "hevc"): JSON invariato garantito.
make_media() { # $1 dir, $2 prefisso
    local dir="$1" prefix="$2"
    local f="$dir/${prefix}_Ep_01.mp4"
    # NB: niente pipe con grep -q (SIGPIPE + pipefail rompono il pipeline);
    # usiamo un file temporaneo per la lista encoder.
    local has_x265=no
    if command -v ffmpeg >/dev/null 2>&1; then
        ffmpeg -hide_banner -encoders >"$RPT/encoders.txt" 2>/dev/null || true
        grep -q 'libx265' "$RPT/encoders.txt" 2>/dev/null && has_x265=yes
    fi
    if [ "$has_x265" = yes ]; then
        ffmpeg -y -v error -f lavfi -i color=c=blue:s=64x64:d=0.3 \
            -c:v libx265 -tag:v hvc1 -pix_fmt yuv420p -movflags +faststart "$f" 2>/dev/null || true
    fi
    if [ ! -s "$f" ]; then
        dd if=/dev/urandom of="$f" bs=1M count=1 status=none 2>/dev/null \
            || dd if=/dev/zero of="$f" bs=1M count=1
    else
        truncate -s 1100000 "$f" 2>/dev/null || true
    fi
}

# Slow server: accetta connessioni e non risponde mai (connessioni tenute
# aperte). Fa bloccare i planning HTTP per ~10s (timeout cpr) senza rete.
start_slow_server() {
    cat > "$RPT/slow_server.py" <<'PY'
import socket, sys
conns = []
s = socket.socket()
s.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
s.bind(("127.0.0.1", 0))
port = s.getsockname()[1]
with open(sys.argv[1], "w") as f:
    f.write(str(port))
s.listen(4)
while True:
    c, _ = s.accept()
    try:
        c.recv(65536)
    except OSError:
        pass
    conns.append(c)
PY
    python3 "$RPT/slow_server.py" "$RPT/slow_port" &
    SLOW_PID=$!
    local i
    for i in $(seq 1 50); do
        [ -s "$RPT/slow_port" ] && break
        sleep 0.1
    done
    SLOW_PORT=$(cat "$RPT/slow_port" 2>/dev/null || echo "")
    if [ -z "$SLOW_PORT" ]; then
        fail "slow server non avviato"
        SLOW_PID=""
        return 1
    fi
    pass "slow server su porta $SLOW_PORT"
}

start_web_server() {
    # NB: niente parsing dello stdout per trovare la porta (il buffer di
    # std::cout non viene flushato finché il processo non esce): si scandisce
    # la stessa gamma di porte che l'app prova (18901-18910).
    XDG_CONFIG_HOME="$WEB_SBX" XDG_CACHE_HOME="$WEB_SBX/cache" \
        "$BIN" --web --port 18901 >"$RPT/web.log" 2>&1 &
    WEB_PID=$!
    PORT=""
    local i p
    for i in $(seq 1 100); do
        kill -0 "$WEB_PID" 2>/dev/null || break
        for p in $(seq 18901 18910); do
            if [ "$(curl -s -o /dev/null -m 1 -w '%{http_code}' "http://127.0.0.1:$p/api/status" 2>/dev/null)" = 200 ]; then
                PORT=$p
                break 2
            fi
        done
        sleep 0.2
    done
    if [ -z "$PORT" ]; then
        fail "server web non avviato (vedi $RPT/web.log)"
        return 1
    fi
    BASE="http://127.0.0.1:$PORT"
    pass "server web su porta $PORT"
}

run_cli() { # $@ → argomenti CLI (con env sandbox)
    XDG_CONFIG_HOME="$CLI_SBX" XDG_CACHE_HOME="$CLI_SBX/cache" "$BIN" "$@"
}

make_media "$CLI_SBX/media/SerieA" SerieA
make_media "$CLI_SBX/media/SerieB" SerieB

# ─────────────────────────── CASI CLI ───────────────────────────
echo "[1/3] CLI: serie vuote"

echo "[]" > "$CLI_SBX/AniDownloader/series_data.json"
set +e
OUT=$(run_cli 2>&1)
CODE=$?
set -e
if [ "$CODE" -eq 0 ]; then pass "JSON serie vuoto → exit 0"; else fail "JSON serie vuoto → exit $CODE"; fi
if echo "$OUT" | grep -q 'Nessuna serie trovata nel database JSON.'; then
    pass "messaggio 'Nessuna serie trovata nel database JSON.'"
else
    fail "messaggio atteso assente: $OUT"
fi

echo "[2/3] CLI: URL non raggiungibili → skip, JSON invariato"

printf '[{"name":"SerieA","service":"animeW_scraper","path":"%s","series_page_url":"https://example.invalid/anime/seriea"},{"name":"SerieB","service":"animeW_scraper","path":"%s","series_page_url":"https://example.invalid/anime/serieb"}]' \
    "$CLI_SBX/media/SerieA" "$CLI_SBX/media/SerieB" > "$CLI_SBX/AniDownloader/series_data.json"
SHA_BEFORE=$(file_sha "$CLI_SBX/AniDownloader/series_data.json")

if run_cli --burst >"$RPT/t2_burst.log" 2>&1; then
    pass "run con URL non raggiungibili → exit 0"
else
    fail "run con URL non raggiungibili → exit $?"
fi
if grep -q 'Tutto aggiornato' "$RPT/t2_burst.log"; then
    pass "'Tutto aggiornato.' mostrato"
else
    fail "'Tutto aggiornato.' assente (vedi $RPT/t2_burst.log)"
fi
if ! grep -q '❌' "$RPT/t2_burst.log"; then
    pass "nessun errore nel run"
else
    fail "errori presenti nel run"
fi
if [ "$SHA_BEFORE" = "$(file_sha "$CLI_SBX/AniDownloader/series_data.json")" ]; then
    pass "sha256 JSON invariato (niente post-run spurio)"
else
    fail "sha256 JSON cambiato dopo il run"
fi

echo "[2b/3] CLI: flusso statico AnimeW (pagina + API episodio + download)"

# Fixture offline del flusso statico (feature 6): pagina serie con la lista
# episodi, endpoint /api/episode/info che restituisce il grabber, e i video.
# Due serie con episodi e video distinti: verifica anche il planning parallelo.
STATIC_SBX="$RPT/static"
mkdir -p "$STATIC_SBX/AniDownloader" "$STATIC_SBX/cache" "$STATIC_SBX/media/Serie" "$STATIC_SBX/media/Serie2"
printf '{"convert_to_h265":false}' > "$STATIC_SBX/AniDownloader/config.json"
dd if=/dev/urandom of="$RPT/video1.mp4" bs=1M count=1 status=none
dd if=/dev/urandom of="$RPT/video2.mp4" bs=1M count=1 status=none
dd if=/dev/urandom of="$RPT/video3.mp4" bs=1M count=1 status=none
dd if=/dev/urandom of="$RPT/video4.mp4" bs=1M count=1 status=none

cat > "$STATIC_SBX/fixture.py" <<'PY'
import http.server, os, sys
MEDIA, PORTFILE = sys.argv[1], sys.argv[2]
SERIE = {1: "EID1", 2: "EID2"}
SERIE2 = {1: "EID3", 2: "EID4"}
class H(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path == "/serie.html":
            body = ('<html><body><a data-episode-num="1" href="/play/test/EID1">Ep1</a>'
                    '<a data-episode-num="2" href="/play/test/EID2">Ep2</a></body></html>').encode()
        elif self.path == "/serie2.html":
            body = ('<html><body><a data-episode-num="1" href="/play/test/EID3">Ep1</a>'
                    '<a data-episode-num="2" href="/play/test/EID4">Ep2</a></body></html>').encode()
        elif self.path.startswith("/api/episode/info?"):
            eid = self.path.split("id=", 1)[1].split("&", 1)[0]
            base = f"http://127.0.0.1:{self.server.server_address[1]}"
            num = {"EID1": 1, "EID2": 2, "EID3": 3, "EID4": 4}.get(eid)
            body = f'{{"grabber":"{base}/video{num}.mp4","name":"{eid}"}}'.encode() if num else b'{"error":true}'
        elif self.path.startswith("/video"):
            p = os.path.join(MEDIA, os.path.basename(self.path))
            if not os.path.exists(p):
                self.send_response(404); self.end_headers(); return
            with open(p, "rb") as f:
                body = f.read()
        else:
            self.send_response(404); self.end_headers(); return
        self.send_response(200)
        self.send_header("Content-Type", "video/mp4" if self.path.startswith("/video") else "text/html")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)
    def log_message(self, *a): pass
srv = http.server.ThreadingHTTPServer(("127.0.0.1", 0), H)
with open(PORTFILE, "w") as f:
    f.write(str(srv.server_address[1]))
srv.serve_forever()
PY
python3 "$STATIC_SBX/fixture.py" "$RPT" "$STATIC_SBX/port" > /dev/null 2>&1 &
FIX_PID=$!
for i in $(seq 1 50); do
    [ -s "$STATIC_SBX/port" ] && break
    sleep 0.1
done
FIX_PORT=$(cat "$STATIC_SBX/port" 2>/dev/null || echo "")
if [ -n "$FIX_PORT" ] && curl -s -m 3 -o /dev/null "http://127.0.0.1:$FIX_PORT/serie.html"; then
    printf '[{"name":"Serie","service":"animeW_scraper","path":"%s","series_page_url":"http://127.0.0.1:%s/serie.html"},{"name":"Serie2","service":"animeW_scraper","path":"%s","series_page_url":"http://127.0.0.1:%s/serie2.html"}]' \
        "$STATIC_SBX/media/Serie" "$FIX_PORT" "$STATIC_SBX/media/Serie2" "$FIX_PORT" > "$STATIC_SBX/AniDownloader/series_data.json"
    SHA_BEFORE=$(file_sha "$STATIC_SBX/AniDownloader/series_data.json")

    if XDG_CONFIG_HOME="$STATIC_SBX" XDG_CACHE_HOME="$STATIC_SBX/cache" \
        ANIDOWNLOADER_API_BASE="http://127.0.0.1:$FIX_PORT" \
        "$BIN" >"$RPT/static_run.log" 2>&1; then
        pass "flusso statico: run completata (exit 0)"
    else
        fail "flusso statico: exit $? (vedi $RPT/static_run.log)"
    fi
    if [ "$(find "$STATIC_SBX/media/Serie" -name '*_Ep_0*.mp4' | wc -l)" -eq 2 ] \
        && [ "$(find "$STATIC_SBX/media/Serie2" -name '*_Ep_0*.mp4' | wc -l)" -eq 2 ]; then
        pass "flusso statico: 4 episodi scaricati (2 serie in parallelo)"
    else
        fail "flusso statico: attesi 4 episodi (2+2): $(ls "$STATIC_SBX/media/Serie" "$STATIC_SBX/media/Serie2" 2>/dev/null | tr '\n' ' ')"
    fi
    if jq -e '.[0].last_downloaded_episode == 2 and .[1].last_downloaded_episode == 2' "$STATIC_SBX/AniDownloader/series_data.json" >/dev/null 2>&1; then
        pass "flusso statico: last_downloaded_episode=2 per entrambe le serie"
    else
        fail "flusso statico: last_downloaded_episode non aggiornato: $(cat "$STATIC_SBX/AniDownloader/series_data.json")"
    fi
    if ! grep -q 'ChromeDriver\|chromedriver' "$RPT/static_run.log"; then
        pass "flusso statico: nessun riferimento a ChromeDriver nel run"
    else
        fail "flusso statico: ChromeDriver ancora referenziato nel run"
    fi
else
    fail "fixture server non avviato: flusso statico non testato"
fi
kill "$FIX_PID" 2>/dev/null || true

echo "[3/3] CLI: SIGINT durante il planning → exit pulito, nessun residuo"

start_slow_server || true
if [ -n "$SLOW_PORT" ]; then
    printf '[{"name":"SerieA","service":"animeW_scraper","path":"%s","series_page_url":"http://127.0.0.1:%s/block"},{"name":"SerieB","service":"animeW_scraper","path":"%s","series_page_url":"http://127.0.0.1:%s/block"}]' \
        "$CLI_SBX/media/SerieA" "$SLOW_PORT" "$CLI_SBX/media/SerieB" "$SLOW_PORT" \
        > "$CLI_SBX/AniDownloader/series_data.json"
    SHA_BEFORE=$(file_sha "$CLI_SBX/AniDownloader/series_data.json")

    run_cli >"$RPT/t3.log" 2>&1 &
    APP_PID=$!
    sleep 1
    kill -INT "$APP_PID" 2>/dev/null || true
    if wait "$APP_PID"; then
        pass "SIGINT durante planning → exit 0"
    else
        fail "SIGINT durante planning → exit $?"
    fi
    if [ "$(find "$CLI_SBX/media" -name '*.aria2' | wc -l)" -eq 0 ]; then
        pass "nessun residuo .aria2 nelle cartelle media"
    else
        fail "residui .aria2 trovati"
    fi
    if [ "$SHA_BEFORE" = "$(file_sha "$CLI_SBX/AniDownloader/series_data.json")" ]; then
        pass "sha256 JSON invariato dopo SIGINT"
    else
        fail "sha256 JSON cambiato dopo SIGINT"
    fi
fi

# ─────────────────────────── CASI API WEB ───────────────────────────
make_media "$WEB_SBX/media/SerieA" SerieA
make_media "$WEB_SBX/media/SerieB" SerieB
printf '[{"name":"SerieA","service":"animeW_scraper","path":"%s","series_page_url":"https://example.invalid/anime/seriea"},{"name":"SerieB","service":"animeW_scraper","path":"%s","series_page_url":"https://example.invalid/anime/serieb"}]' \
    "$WEB_SBX/media/SerieA" "$WEB_SBX/media/SerieB" > "$WEB_SBX/AniDownloader/series_data.json"

start_web_server || true
if [ -z "${PORT:-}" ]; then
    echo "SMOKE: server web non disponibile, salto i test API"
    exit 1
fi

echo "[4/14] SPA e routing"
if [ "$(http_code "$BASE/")" = 200 ] && curl -s -m 10 "$BASE/" | grep -qi '<!DOCTYPE html>'; then
    pass "GET / → index.html (SPA)"
else
    fail "GET / non restituisce index.html"
fi
if [ "$(http_code "$BASE/pagina/inesistente")" = 200 ]; then
    pass "GET /pagina/inesistente → fallback SPA 200"
else
    fail "fallback SPA non attivo"
fi
if [ "$(http_code "$BASE/api/nope")" = 404 ]; then
    pass "GET /api/nope → 404"
else
    fail "GET /api/nope non 404"
fi

echo "[4b/14] cache header asset (feature 3)"
ASSET_JS=$(curl -s -m 10 "$BASE/" | grep -o 'assets/[a-zA-Z0-9_.-]*\.js' | head -1 || true)
if [ -n "$ASSET_JS" ]; then
    ASSET_HDR=$(curl -s -I -m 10 "$BASE/$ASSET_JS" | grep -i '^cache-control:' | tr -d '\r' || true)
    if echo "$ASSET_HDR" | grep -qi 'max-age=31536000'; then
        pass "asset hashato ($ASSET_JS) → cache immutable"
    else
        fail "asset hashato senza cache lunga: $ASSET_HDR"
    fi
else
    fail "nessun asset JS hashato trovato nella SPA"
fi
ROOT_HDR=$(curl -s -I -m 10 "$BASE/" | grep -i '^cache-control:' | tr -d '\r' || true)
if echo "$ROOT_HDR" | grep -qi 'no-cache'; then
    pass "index.html → no-cache"
else
    fail "index.html senza no-cache: $ROOT_HDR"
fi

echo "[5/14] /api/status"
curl -s -m 10 "$BASE/api/status" > "$RPT/status.json"
if jq -e '.version == "2.0.1"' "$RPT/status.json" >/dev/null; then
    pass "version == 2.0.1"
else
    fail "version attesa 2.0.1: $(cat "$RPT/status.json")"
fi
if jq -e --argjson p "$PORT" '.port == $p' "$RPT/status.json" >/dev/null; then
    pass "port == $PORT"
else
    fail "port attesa $PORT: $(cat "$RPT/status.json")"
fi

echo "[6/14] /api/series e ordinamento"
curl -s -m 10 "$BASE/api/series" > "$RPT/series.json"
if jq -e '.series | length == 2' "$RPT/series.json" >/dev/null; then
    pass "N serie == 2"
else
    fail "attese 2 serie: $(cat "$RPT/series.json")"
fi
curl -s -m 10 "$BASE/api/series?sort=name&dir=desc" > "$RPT/series_desc.json"
if jq -e '[.series[].name] == ["SerieB", "SerieA"]' "$RPT/series_desc.json" >/dev/null; then
    pass "sort=name&dir=desc coerente"
else
    fail "ordinamento errato: $(cat "$RPT/series_desc.json")"
fi

echo "[7/14] CRUD serie"
curl -s -m 10 -X POST "$BASE/api/series" \
    -d "{\"name\":\"SerieC\",\"service\":\"animeW_scraper\",\"path\":\"$WEB_SBX/media/SerieC\",\"series_page_url\":\"https://example.invalid/anime/seriec\"}" \
    > "$RPT/crud_post.json"
if jq -e '.success == true and .index == 2' "$RPT/crud_post.json" >/dev/null; then
    pass "POST aggiunge serie (index 2)"
else
    fail "POST fallito: $(cat "$RPT/crud_post.json")"
fi
curl -s -m 10 -X PUT "$BASE/api/series/0" \
    -d "{\"name\":\"SerieA-renamed\",\"service\":\"animeW_scraper\",\"path\":\"$WEB_SBX/media/SerieA\",\"series_page_url\":\"https://example.invalid/anime/seriea\"}" \
    > "$RPT/crud_put.json"
if jq -e '.success == true' "$RPT/crud_put.json" >/dev/null; then
    pass "PUT aggiorna serie"
else
    fail "PUT fallito: $(cat "$RPT/crud_put.json")"
fi
curl -s -m 10 -X DELETE "$BASE/api/series/2" > "$RPT/crud_delete.json"
if jq -e '.success == true' "$RPT/crud_delete.json" >/dev/null; then
    pass "DELETE rimuove serie"
else
    fail "DELETE fallito: $(cat "$RPT/crud_delete.json")"
fi
if [ "$(http_code -X PUT "$BASE/api/series/99" -d '{"name":"x"}')" = 404 ]; then
    pass "PUT fuori range → 404"
else
    fail "PUT fuori range non 404"
fi
if [ "$(http_code -X DELETE "$BASE/api/series/99")" = 404 ]; then
    pass "DELETE fuori range → 404"
else
    fail "DELETE fuori range non 404"
fi
if [ "$(http_code -X POST "$BASE/api/series" -d 'non-json')" = 400 ]; then
    pass "POST body non-JSON → 400"
else
    fail "POST body non-JSON non 400"
fi
curl -s -m 10 "$BASE/api/series" > "$RPT/series2.json"
if jq -e '.series | length == 2' "$RPT/series2.json" >/dev/null; then
    pass "dopo CRUD restano 2 serie"
else
    fail "conteggio serie errato dopo CRUD: $(cat "$RPT/series2.json")"
fi

# Ripristino stato canonico: SerieA veloce, SerieB lenta (planning bloccato 10s)
curl -s -m 10 -X PUT "$BASE/api/series/0" \
    -d "{\"name\":\"SerieA\",\"service\":\"animeW_scraper\",\"path\":\"$WEB_SBX/media/SerieA\",\"series_page_url\":\"https://example.invalid/anime/seriea\"}" \
    >/dev/null
if [ -n "$SLOW_PORT" ]; then
    curl -s -m 10 -X PUT "$BASE/api/series/1" \
        -d "{\"name\":\"SerieB\",\"service\":\"animeW_scraper\",\"path\":\"$WEB_SBX/media/SerieB\",\"series_page_url\":\"http://127.0.0.1:$SLOW_PORT/block\"}" \
        >/dev/null
fi

echo "[8/14] /api/config"
curl -s -m 10 "$BASE/api/config" > "$RPT/cfg1.json"
if jq -e '.config.log_file_path | length > 0' "$RPT/cfg1.json" >/dev/null; then
    pass "GET config: log_file_path presente"
else
    fail "config senza log_file_path: $(cat "$RPT/cfg1.json")"
fi
curl -s -m 10 -X PUT "$BASE/api/config" -d '{"convert_to_h265":false}' > "$RPT/cfg_put.json"
if jq -e '.success == true' "$RPT/cfg_put.json" >/dev/null; then
    pass "PUT config accettato"
else
    fail "PUT config fallito: $(cat "$RPT/cfg_put.json")"
fi
curl -s -m 10 "$BASE/api/config" > "$RPT/cfg2.json"
if jq -e '.config.convert_to_h265 == false' "$RPT/cfg2.json" >/dev/null; then
    pass "campo scritto letto correttamente"
else
    fail "campo config non aggiornato"
fi
if [ "$(http_code -X PUT "$BASE/api/config" -d 'non-json')" = 400 ]; then
    pass "PUT config body invalido → 400"
else
    fail "PUT config body invalido non 400"
fi
curl -s -m 10 -X PUT "$BASE/api/config" -d '{"convert_to_h265":true}' >/dev/null
curl -s -m 10 "$BASE/api/config" > "$RPT/cfg3.json"
if jq -e '.config.convert_to_h265 == true' "$RPT/cfg3.json" >/dev/null; then
    pass "config ripristinata"
else
    fail "config non ripristinata"
fi

echo "[9/14] /api/browse (regressione mtime)"
curl -s -m 10 --get --data-urlencode "path=$WEB_SBX/media" "$BASE/api/browse" > "$RPT/browse.json"
if jq -e '.entries | length == 2' "$RPT/browse.json" >/dev/null; then
    pass "browse: 2 voci"
else
    fail "browse: voci attese 2: $(cat "$RPT/browse.json")"
fi
if jq -e '[.entries[].mtime] | all(. > 0)' "$RPT/browse.json" >/dev/null; then
    pass "browse: mtime positivi (niente epoch 2174)"
else
    fail "mtime non positivi: $(cat "$RPT/browse.json")"
fi
curl -s -m 10 --get --data-urlencode "path=/nonexistent" "$BASE/api/browse" > "$RPT/browse2.json"
if jq -e '.entries | length == 0' "$RPT/browse2.json" >/dev/null; then
    pass "browse: path inesistente → lista vuota"
else
    fail "browse: path inesistente non vuoto: $(cat "$RPT/browse2.json")"
fi

echo "[10/14] /api/log"
curl -s -m 10 "$BASE/api/log?lines=10" > "$RPT/log.json"
if jq -e '.lines | length >= 1' "$RPT/log.json" >/dev/null; then
    pass "log: righe presenti"
else
    fail "log vuoto: $(cat "$RPT/log.json")"
fi

echo "[11/14] description e poster"
curl -s -m 10 --get --data-urlencode "path=$WEB_SBX/media/SerieA" "$BASE/api/description" > "$RPT/desc1.json"
if jq -e '.description == ""' "$RPT/desc1.json" >/dev/null; then
    pass "description senza NFO → vuota"
else
    fail "description attesa vuota: $(cat "$RPT/desc1.json")"
fi
printf '<tvshow><plot>Descrizione di test</plot></tvshow>' > "$WEB_SBX/media/SerieA/tvshow.nfo"
curl -s -m 10 --get --data-urlencode "path=$WEB_SBX/media/SerieA" "$BASE/api/description" > "$RPT/desc2.json"
if jq -e '.description == "Descrizione di test"' "$RPT/desc2.json" >/dev/null; then
    pass "description con NFO letta"
else
    fail "description NFO errata: $(cat "$RPT/desc2.json")"
fi
POSTER_CT=$(curl -s -m 10 -o "$RPT/poster.svg" -w '%{content_type}' \
    --get --data-urlencode "path=$WEB_SBX/media/SerieA" "$BASE/api/poster")
if [ "$(http_code --get --data-urlencode "path=$WEB_SBX/media/SerieA" "$BASE/api/poster")" = 200 ] \
    && [ "$POSTER_CT" = "image/svg+xml" ]; then
    pass "poster → placeholder SVG (200, image/svg+xml)"
else
    fail "poster placeholder errato (CT: $POSTER_CT)"
fi

echo "[12/14] doppio start → 409 (TOCTOU)"
SHA_DL_BEFORE=$(file_sha "$WEB_SBX/AniDownloader/series_data.json")
curl -s -N -m 30 "$BASE/api/download/events" > "$RPT/sse.log" 2>&1 &
SSE_PID=$!
sleep 1
curl -s -m 5 -X POST "$BASE/api/download/start" -d '{}' > "$RPT/dl1.json" &
P1=$!
curl -s -m 5 -X POST "$BASE/api/download/start" -d '{}' > "$RPT/dl2.json" &
P2=$!
wait "$P1" || true
wait "$P2" || true
DL_200=""
DL_409=""
if jq -e '.success == true' "$RPT/dl1.json" >/dev/null 2>&1; then
    DL_200=dl1; DL_409=dl2
else
    DL_200=dl2; DL_409=dl1
fi
if jq -e '.success == true' "$RPT/$DL_200.json" >/dev/null; then
    pass "primo start accettato"
else
    fail "primo start fallito: $(cat "$RPT/$DL_200.json")"
fi
if jq -e '.code == 409' "$RPT/$DL_409.json" >/dev/null; then
    pass "doppio POST /download/start → 409"
else
    fail "doppio start non 409: $(cat "$RPT/$DL_409.json")"
fi

echo "[13/14] stop, running=false, JSON invariato"
curl -s -m 10 "$BASE/api/download/status" > "$RPT/status2.json"
if jq -e '.running == true' "$RPT/status2.json" >/dev/null; then
    pass "download in corso (running=true)"
else
    fail "running atteso true: $(cat "$RPT/status2.json")"
fi
curl -s -m 10 -X POST "$BASE/api/download/stop" > "$RPT/stop.json"
if jq -e '.success == true and .message == "Stop signal sent"' "$RPT/stop.json" >/dev/null; then
    pass "stop → 'Stop signal sent'"
else
    fail "stop inatteso: $(cat "$RPT/stop.json")"
fi
RUNNING=true
for i in $(seq 1 40); do
    RUNNING=$(curl -s -m 3 "$BASE/api/download/status" | jq -r '.running' 2>/dev/null || echo true)
    [ "$RUNNING" = "false" ] && break
    sleep 1
done
if [ "$RUNNING" = "false" ]; then
    pass "running=false dopo lo stop"
else
    fail "ancora running dopo 40s"
fi
if [ "$SHA_DL_BEFORE" = "$(file_sha "$WEB_SBX/AniDownloader/series_data.json")" ]; then
    pass "JSON invariato dopo start+stop"
else
    fail "JSON modificato dopo start+stop"
fi

echo "[14/14] SSE su /api/download/events"
sleep 1
kill "$SSE_PID" 2>/dev/null || true
wait "$SSE_PID" 2>/dev/null || true
SSE_PID=""
if grep -q '"type":"overall"' "$RPT/sse.log"; then
    pass "SSE: evento overall ricevuto"
else
    fail "SSE: nessun evento overall (vedi $RPT/sse.log)"
fi
if grep -q '"type":"done"' "$RPT/sse.log"; then
    pass "SSE: evento done ricevuto"
else
    fail "SSE: nessun evento done (vedi $RPT/sse.log)"
fi
if [ "$(wc -l < "$RPT/sse.log")" -ge 4 ]; then
    pass "SSE: flusso attivo durante start+stop"
else
    fail "SSE: flusso troppo corto (vedi $RPT/sse.log)"
fi

# ─────────────────────────── VERIFICHE FINALI ───────────────────────────
if [ -n "$SHA_REAL_BEFORE" ]; then
    SHA_REAL_AFTER=$(tar -C "$HOME/.config" -cf - AniDownloader 2>/dev/null | sha256sum | cut -d' ' -f1)
    if [ "$SHA_REAL_BEFORE" = "$SHA_REAL_AFTER" ]; then
        pass "config reale ~/.config/AniDownloader intatta"
    else
        fail "config reale modificata dai test!"
    fi
else
    if [ -e "$REAL_CFG" ]; then
        fail "config reale creata dai test!"
    else
        pass "config reale non toccata (assente)"
    fi
fi

echo
echo "==== SMOKE SUMMARY: $PASS passati, $FAILS falliti ===="
echo "Report: $RPT"
if [ "$FAILS" -eq 0 ]; then
    echo "SMOKE: OK"
    exit 0
fi
echo "SMOKE: FAIL"
exit 1
