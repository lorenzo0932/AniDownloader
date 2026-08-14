# Fixture di test — provenienza

Le fixture sono **minimizzate**: contengono solo i nodi rilevanti per il parsing,
non pagine complete. Un cambio cosmetico del sito non deve rendere obsolete le
fixture (vedi plan/10-testing-scrapers-coverage.md).

| File | Sito | Provenienza | Uso |
|---|---|---|---|
| `animew_page.html` | AnimeWorld | Campionata da `animeworld.ac/play/...` (14 ago 2026): tag `<a data-episode-num data-id ... href>`. Episodi volutamente non ordinati + 1 tag senza `href` | `AnimeWScraper::parseSeriesPage` |
| `animew_no_episodes.html` | AnimeWorld | Struttura alterata (niente `data-episode-num`) | Lista vuota attesa |
| `animew_episode_info.json` | AnimeWorld | Campionata da `/api/episode/info?id=...&alt=1` (14 ago 2026): `{"grabber": ..., "name": ..., "target": ...}` | `AnimeWScraper::parseEpisodeInfo` |
| `animew_episode_info_error.json` | AnimeWorld | Variante `{"error": true}` | Grabber vuoto atteso |
| `animeu_page.html` | AnimeUnity | Il sito live carica gli episodi via JS: markup storico con `class="episode-item"` (quello gestito dalla regex). Episodi non ordinati | `AnimeUScraper::parseSeriesPage` |
| `animeu_episode_page.html` | AnimeUnity | Markup storico con `<iframe id="embed" src="...">` | `AnimeUScraper::parseEpisodePage` |
| `animeu_embed_page.html` | AnimeUnity | Markup storico con `window.downloadUrl = "..."` | `AnimeUScraper::parseEmbedPage` |
| `animeu_episode_no_iframe.html` | AnimeUnity | Variante senza `id="embed"` | URL iframe vuoto atteso |

Nessuna fixture richiede rete: i test le leggono da disco e le passano
direttamente ai parser.
