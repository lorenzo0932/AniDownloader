## Titolo (conventional commits)

```
<type>(<scope>): <sintesi>

es. fix(core): nextNeeded corretto con media vuota e lastDownloaded>0 (fix #12)
```

Tipo: `feat` `fix` `perf` `refactor` `test` `chore` `ci` `docs` `style`.

## Descrizione

- Cosa cambia (1-3 righe):
- Perché (riferimento a issue/plan se presente):
- Test aggiunti/aggiornati:

## Checklist

- [ ] CI verde (build + test_core + test_media + smoke)
- [ ] Test nuovo/aggiornato per la modifica (se applicabile)
- [ ] CHANGELOG.md aggiornato (sezione [Unreleased])
- [ ] Nessun file generato committato (web/dist, src-tauri/target, embedded_web.*)
- [ ] Nessun dato utente committato (series_data.json)

## Note per il merge

- Merge con **squash** (il titolo della PR diventa il messaggio del commit).
- Il branch viene cancellato automaticamente dopo il merge.
