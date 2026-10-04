# ADR-004: rewrite architetturale — SQLite vendored + demone single-process

- Stato: **accettata** (2026-10-04)
- Contesto: JSON `series_data.json` + due processi (`anidownloaderd` +
  `anidownloader-check.timer`) + `pkill -9` come stop + concorrenza senza
  transazioni. Superata ADR-003 (rewrite differito).
- Addendum 2026-10-04 sera: E1 approvata come B6 (engine a code asincrone).

## Decisioni vincolanti

1. **SQLite vendored** in `third_party/` (amalgamation), licenza in
   `licenses/` + riga in `THIRD_PARTY_NOTICES.md`. Niente dipendenza di
   sistema: build riproducibile su Linux/Windows/macOS.
2. **Scheduler interno** al demone: intervalli da config (default 15'),
   catch-up post-sleep (un giro subito, mai arretrati), backoff su errori
   di rete, stop pulito (finisce l'episodio in corso), eventi verso il
   bus SSE esistente.
3. **Migrazione JSON→SQLite**: backup `.bak` timestamped prima di tutto,
   dry-run, verifica conteggi + hash per campo, rollback documentato;
   la CLI resta sempre capace di leggere il JSON vecchio.
4. **Timer OS rimossi** dagli installer (`anidownloader-check.timer`,
   Scheduled Task, launchd check): resta solo l'autostart del demone
   (systemd unit, logon task, launchd plist — cfr. ADR-002).
5. **Cutover misurabile**: test verdi + shadow-run dual-write senza
   divergenze per 7 giorni su impianto reale + canale `--dev` prima di
   stabile. `main` non si tocca finché settimane di esercizio reale non
   escludono bug.
6. **Notifiche**: master ON/OFF (default OFF) + sorgente solo UI /
   solo backend / entrambi + icona `logo.png`. Backend senza schermo:
   casella-con-badge in UI.
7. **(2-bis) Schedulazione da UI**: toggle ON/OFF + intervallo 5'–24h
   live, giro in corso mai interrotto, default ON/15', persistito in
   `scheduling:{abilitato,intervalloMinuti}`.
8. **CLI**: resta manuale/one-shot + debug; silent-schedulato in pensione;
   `--web` diventa il demone completo (web + scheduler).

## Fasi (dettagli in `plan/soa-rewrite.md`, gitignored)

B1 `Database` + schema → B2 importatore → B3 cutover (`BREAKING CHANGE:`,
major `3.0.0`) → B4 scheduler + settings UI → B5 autostart 3 OS →
B6 engine a code (tabella `tasks`, worker pool, backoff, stop cooperativo).

## Riferimenti

- `plan/soa-rewrite.md` (B0–B6, specifica estesa)
- `plan/adr-003-rewrite-deferred.md` (superata da questa ADR)
- `plan/adr-002-windows-logon-task.md`, `plan/adr-001-mtime-over-rewrite.md`
