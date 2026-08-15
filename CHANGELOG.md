# Changelog

Tutte le modifiche notevoli a questo progetto sono documentate in questo file.

Il formato si basa su [Keep a Changelog](https://keepachangelog.com/it/1.1.0/),
e il progetto aderisce al [Semantic Versioning](https://semver.org/lang/it/).

## [Unreleased]

### Aggiunto

- Igiene CI (feature 8): job sanitizer (ASan+UBSan) su Linux, ccache,
  check `clang-format` in CI, report `clang-tidy`, `.clang-format` e
  `.clang-tidy` in root, `.git-blame-ignore-revs`.
- CHANGELOG.md (questo file).
- Template PR (`.github/PULL_REQUEST_TEMPLATE.md`).
- Ruleset GitHub `dev-soft (GitHub Flow)` su `dev` e repo settings:
  squash-only merge + delete branch on merge.
- Test unit offline dei parser (feature 10): `test_scraper` con fixture
  HTML/JSON minimizzate in `tests/fixtures/` (parse pagine/episodi, titolo,
  casi limite), senza rete.
- Job CI `coverage`: build con `--coverage`, report lcov/genhtml (branch
  coverage) come artifact + riepilogo nel log (Linux, nessuna soglia).
- Feature 11 — robustezza pipeline download:
  - Download atomico: aria2 scarica in `file.part`, check di integrità
    (ffmpeg `-xerror`) sul `.part`, poi publish atomico no-replace
    (`renameat2(RENAME_NOREPLACE)` / `renamex_np(RENAME_EXCL)` /
    `MoveFileExW` senza replace) — **nessun file corrotto può finire in media**.
  - Resume: `aria2 --continue=true`, i parziali non vengono più cancellati
    tra i tentativi né su chiusura (nuova chiave `resume_interrupted_downloads`,
    default `true`). Recupero automatico dei `.part`+`.aria2` al run successivo.
  - Lock transazionale di processo (`flock`/handle esclusivo) su planning →
    download → salvataggio stato: un'avvio manuale durante un run attivo viene
    rifiutato con messaggio chiaro.
  - Fix quirk S4: media vuota + `last_downloaded_episode>0` → riparte da
    `last+1` invece di riscaricare tutto (test aggiornati: unit + smoke).

### Modificato

- CI: build solo su push a `main`/`dev` con `paths-ignore` solo-docs,
  `workflow_dispatch` manuale, `concurrency` con cancel-in-progress.
- Formattazione applicata a tutto il codice C++ (`clang-format`, LLVM).
- **Config**: `auto_cleanup_on_close` deprecata — la nuova
  `resume_interrupted_downloads` (default `true`) vince su di essa. Matrice di
  migrazione nel loader: `auto_cleanup_on_close=true` esplicito senza la nuova
  chiave → comportamento vecchio preservato (`resume=false`); chiave assente →
  nuovo default (partials trattenuti per il resume). Il cleanup su chiusura
  agisce solo con `resume=false`.
- Cleanup parziali su stop: non tocca più il nome finale (sempre pubblicato e
  valido), solo `.part`/`.part.aria2`, e solo con `resume=false`.
- `AGENTS.md` e i documenti di piano spostati fuori dal trunk
  (solo locali: `local/reference`).
- Standard C++ aggiornato a C++20 (feature 9): `std::format` al posto di
  `ostringstream`/`to_string` nei log e nella formattazione (output
  byte-identico, pinnato dai test), `std::jthread` per i worker di analisi
  (join RAII, stop-path con detach a latenza invariata), designated
  initializers su `EpisodeInfo`/`TaskReport`, `std::string_view` su
  `getRecentLines`/`expandTilde`.

## [2.0.1] - 2026

- Web server: fix TOCTOU su `POST /api/download/start` (claim atomico → 409).
- Scraper AnimeW: flusso statico senza ChromeDriver (`/api/episode/info`).
- Scritture JSON atomiche (tmp + rename) per config e series_data.
- Health-check unificato in PlanningService.
- Cache `local_episode_count` con TTL 5s e mtime dir.
- E2E smoke offline (S1-S5) + check web, registrato come CTest.
