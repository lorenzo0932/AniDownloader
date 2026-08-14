#pragma once
#include <string>

namespace Core {

    // Task di download/conversione pianificato da uno scraper.
    // Vive nel core (non negli scrapers): core non deve dipendere da scrapers.
    struct DownloadTask {
        bool shouldProcess = false;
        std::string videoUrl;
        int episodeNumber = -1;
        std::string fileName;
        std::string errorMessage;
    };

}
