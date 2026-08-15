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

### Modificato

- CI: build solo su push a `main`/`dev` con `paths-ignore` solo-docs,
  `workflow_dispatch` manuale, `concurrency` con cancel-in-progress.
- Formattazione applicata a tutto il codice C++ (`clang-format`, LLVM).
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
