#pragma once

#include "core/Series.hpp"
#include <string>

namespace Core {

    // In C++ non usiamo dizionari generici per i dati strutturati.
    // Creiamo una struct che definisce esattamente cosa deve restituire lo scraper.
    struct DownloadTask {
        bool shouldProcess = false;  // Sostituisce l'azione 'process' o 'skip'
        std::string videoUrl;        // L'URL diretto del video (es. .mp4)
        int episodeNumber = -1;      // Il numero dell'episodio trovato
        std::string fileName;        // Il nome finale del file da salvare
        std::string errorMessage;    // Se fallisce, mettiamo qui l'errore
    };

    class BaseScraper {
    public:
        // IN C++ È FONDAMENTALE avere un distruttore virtuale nelle classi base
        // Altrimenti rischi memory leak quando distruggi gli scraper figli.
        virtual ~BaseScraper() = default;

        // Metodo virtuale puro (L'equivalente esatto di @abstractmethod)
        // Prende in input una Serie (sola lettura: const &) e restituisce un Task
        virtual DownloadTask planSeriesTask(const Series& series) = 0;
    };

}