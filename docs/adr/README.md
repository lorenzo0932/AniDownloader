# Architecture Decision Records

Decisioni architetturali vincolanti di AniDownloader. Un ADR è immutabile:
correzioni solo via addendum datato in calce al file.

| N. | Titolo | Stato |
|----|--------|-------|
| 0004 | [Rewrite: SQLite vendored + demone single-process](0004-sqlite-rewrite.md) | accettata (2026-10-04) |

Le ADR 0001–0003 restano bozze nella cartella locale `plan/` (gitignored),
non promosse a documentazione di progetto:

| N. | Titolo | Stato |
|----|--------|-------|
| 0001 | Rivalidazione mtime invece di rewrite | implementata (Fase 0), superata dal cutover DB |
| 0002 | Daemon Windows come logon-task | attuata in B5 |
| 0003 | Rewrite SoA differito con trigger | superata da ADR-004 |

Solo la 0004 è stata promossa: le altre non hanno valore normativo per chi
legge il repository pubblico.
