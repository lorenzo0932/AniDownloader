#pragma once
#include <string>

namespace Core {
    // Identità canonica di una serie (B0, parità Scopri/Gestione).
    // Solo C++: il frontend confronta stringhe opache dal server, mai
    // normalizzazione JS. Nessuna rete: funzione pura e deterministica.
    //
    // Contratto conservativo (nessuna equivalenza inventata):
    // - Il servizio fa parte dell'identità: stesso path su servizi diversi
    //   resta diverso. Stesso titolo con URL diverso resta diverso.
    // - Host case-insensitive, path case-sensitive (gli ID AW mescolano
    //   maiuscole/minuscole: NON dedurre case-insensitività del path).
    // - Mirror solo tramite allowlist verificata nel codice:
    //   AW: www.animeworld.ac (canonico, vedi kAnimeWorldBaseUrl) + .so
    //   (301 a .ac, commento in AnimeWScraper.cpp); AU: animeunity.to
    //   (docs/SCRAPERS.md) + .so (fixture storica tests/fixtures/animeu_page.html).
    // - Path relativo AW del catalogo (/play/slug.ID) risolto contro la base
    //   ufficiale: equivale all'assoluto sullo stesso host canonico.
    // - Fragment sempre scartato (mai inviato al server), trailing slash
    //   scartato, query scartata (le pagine serie/catalogo dei due servizi
    //   non usano la query per distinguere le serie: pageUrl del discover
    //   non ne ha, lo scraper fetcha l'URL così com'è).
    // - URL di episodio AW (/play/slug.ID/<epId>) NON collassa sull'identità
    //   della serie: resta distinto (fallback con path completo).
    // - Forme non riconosciute: fallback conservativo servizio + URL completo
    //   (mai cancellare host/query/path significativi, mai merge aggressivi).
    std::string seriesIdentity(const std::string& service, const std::string& url);

    // Scorciatoia per gli item del catalogo Scopri (sempre AnimeW):
    // accetta il pageUrl relativo (/play/...) o assoluto.
    std::string discoverIdentity(const std::string& pageUrl);
} // namespace Core
