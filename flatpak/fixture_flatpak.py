#!/usr/bin/env python3
"""Fixture offline per il test end-to-end del core dentro il flatpak.

Replica il flusso AnimeW usato da smoke.sh: una pagina serie con
data-episode-num, un endpoint /api/episode/info che restituisce il grabber,
e un video di prova servito localmente. Il sidecar C++ dentro il sandbox
flatpak scarica il video via aria2c → verifichiamo che il core funzioni.

Uso: python3 fixture_flatpak.py <porta> <dir_video>
"""
import http.server
import os
import sys
import threading

PORT = int(sys.argv[1])
MEDIA = sys.argv[2]

VIDEO_BYTES = None


def make_video():
    global VIDEO_BYTES
    path = os.path.join(MEDIA, "video1.mp4")
    if not os.path.exists(path):
        # Video finto valido: header MP4 + payload ≥ 1MB (isMediaFileHealthy
        # richiede >= 1MB). Non è un video reale ma supera il check dimensionale;
        # il download/conversione non sono attivati (convert_to_h265=false).
        with open(path, "wb") as f:
            f.write(b"\x00\x00\x00\x18ftypmp42" + b"\x00" * (1_100_000 - 16))
    with open(path, "rb") as f:
        VIDEO_BYTES = f.read()


class H(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path == "/serie.html":
            body = (
                '<html><body>'
                '<a data-episode-num="1" href="/play/test/EID1">Ep1</a>'
                '</body></html>'
            ).encode()
            ctype = "text/html"
        elif self.path.startswith("/api/episode/info?"):
            eid = self.path.split("id=", 1)[1].split("&", 1)[0]
            if eid == "EID1":
                base = f"http://127.0.0.1:{PORT}"
                body = f'{{"grabber":"{base}/video1.mp4","name":"EID1"}}'.encode()
                ctype = "application/json"
            else:
                body = b'{"error":true}'
                ctype = "application/json"
        elif self.path.startswith("/video1.mp4"):
            body = VIDEO_BYTES
            ctype = "video/mp4"
        else:
            self.send_response(404)
            self.end_headers()
            return
        self.send_response(200)
        self.send_header("Content-Type", ctype)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *a):
        pass


def main():
    make_video()
    srv = http.server.ThreadingHTTPServer(("127.0.0.1", PORT), H)
    print(f"fixture su http://127.0.0.1:{PORT}", flush=True)
    srv.serve_forever()


if __name__ == "__main__":
    main()
