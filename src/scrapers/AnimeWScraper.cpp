#include "scrapers/AnimeWScraper.hpp"
#include "scrapers/ScraperUtils.hpp"
#include <cpr/cpr.h>
#include <nlohmann/json.hpp>
#include <thread>
#include <chrono>
#include <iostream>
#include <regex>

using json = nlohmann::json;

namespace Core {

// Helper per inviare comandi HTTP a ChromeDriver (porta 9515)
static json webdriverCommand(const std::string& endpoint, const std::string& method, const json& payload = {}) {
    std::string url = "http://localhost:9515" + endpoint;
    cpr::Response r;
    cpr::Header headers = {{"Content-Type", "application/json"}};
    
    if (method == "POST") {
        r = cpr::Post(cpr::Url{url}, cpr::Body{payload.dump()}, headers);
    } else if (method == "GET") {
        r = cpr::Get(cpr::Url{url});
    } else if (method == "DELETE") {
        r = cpr::Delete(cpr::Url{url});
    }
    
    if (r.status_code != 200) {
        std::cerr << "[DEBUG ERROR] WebDriver HTTP " << method << " " << endpoint 
                  << " fallito con status " << r.status_code 
                  << " | Risposta: " << r.text << std::endl;
    }
    
    if (r.status_code == 200 && !r.text.empty()) {
        try { return json::parse(r.text); } catch (...) { return json(); }
    }
    return json();
}

// Estrae l'URL dal log grezzo di Chrome pulendo i backslash di escape (Metodo IDM)
static std::string extractUrlFromLog(const std::string& msgStr) {
    std::regex urlRegex(R"raw(https?:\\?/\\?/[^\s"']+)raw");
    std::smatch match;
    std::string::const_iterator searchStart(msgStr.cbegin());
    
    while (std::regex_search(searchStart, msgStr.cend(), match, urlRegex)) {
        std::string foundUrl = match[0].str();
        
        std::string cleanUrl = "";
        for (char c : foundUrl) {
            if (c != '\\') cleanUrl += c;
        }
        
        if (!cleanUrl.empty() && (cleanUrl.back() == '"' || cleanUrl.back() == '\'')) {
            cleanUrl.pop_back();
        }

        if ((cleanUrl.find(".mp4") != std::string::npos || cleanUrl.find("m3u8") != std::string::npos) && 
            cleanUrl.find("blob:") == std::string::npos) {
            return cleanUrl;
        }
        searchStart = match.suffix().first;
    }
    return "";
}

DownloadTask AnimeWScraper::planSeriesTask(const Series& series) {
    DownloadTask task;
    task.shouldProcess = false;

    std::cout << "[DEBUG] --- Inizio analisi statica per: " << series.name << " ---" << std::endl;

    // --- FASE 1: ANALISI STATICA VELOCE CON CPR (SENZA AVVIARE CHROME) ---
    cpr::Header staticHeaders = {
        {"User-Agent", "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/122.0.0.0 Safari/537.36"},
        {"Referer", "https://www.animeworld.so/"}
    };
    
    std::string html = "";
    try {
        cpr::Response r = cpr::Get(cpr::Url{series.seriesPageUrl}, staticHeaders, cpr::VerifySsl{false}, cpr::Timeout{10000});
        if (r.status_code == 200) {
            html = r.text;
        }
    } catch (...) {}

    if (html.empty()) {
        task.errorMessage = "Errore: Impossibile scaricare l'HTML statico per la verifica preliminare.";
        std::cerr << "[DEBUG ERROR] Richiesta statica fallita per: " << series.name << std::endl;
        return task;
    }

    // Estrazione episodi dall'HTML statico tramite Regex
    std::regex epTagRegex(R"raw(<a[^>]+data-episode-num=["'](\d+)["'][^>]*>)raw");
    struct EpData { int n; std::string url; };
    std::vector<EpData> found;

    auto it = std::sregex_iterator(html.begin(), html.end(), epTagRegex);
    for (; it != std::sregex_iterator(); ++it) {
        std::regex hrefRegex(R"raw(href=["']([^"']+)["'])raw");
        std::smatch m;
        std::string tag = (*it)[0].str();
        if (std::regex_search(tag, m, hrefRegex)) {
            found.push_back({
                std::stoi((*it)[1].str()), 
                m[1].str()
            });
        }
    }

    if (found.empty()) {
        task.errorMessage = "Nessun episodio rilevato nell'HTML statico.";
        std::cerr << "[DEBUG ERROR] Regex fallita o pagina vuota per: " << series.name << std::endl;
        return task;
    }
    std::sort(found.begin(), found.end(), [](const EpData& a, const EpData& b) { return a.n < b.n; });

    // Calcolo dell'episodio necessario su disco
    int nextNeeded = ScraperUtils::getNextEpisodeNum(series.path);
    EpData target = {0, ""};
    int finalEpNum = 0;
    
    for (const auto& ep : found) {
        int local = series.continueSeries ? (ep.n + series.passedEpisodes) : ep.n;
        if (local >= nextNeeded) { target = ep; finalEpNum = local; break; }
    }

    // Prova del nove: Se siamo in pari, terminiamo SUBITO senza consumare risorse
    if (target.url.empty()) {
        std::cout << "[DEBUG] " << series.name << " e' gia' in pari (Episodio su disco: " << nextNeeded - 1 << "). Salto avvio ChromeDriver." << std::endl;
        task.shouldProcess = false;
        return task;
    }

    // --- FASE 2: AVVIO CHROMEDRIVER (SOLO SE C'E' UN NUOVO EPISODIO) ---
    std::cout << "[DEBUG] Rilevato nuovo episodio: Ep." << finalEpNum << ". Avvio ChromeDriver..." << std::endl;

    json caps = {
        {"capabilities", {
            {"alwaysMatch", {
                {"goog:chromeOptions", {
                    {"args", {
                        "--headless=new", // Riconfigurato in modalità invisibile ultra-performante
                        "--no-sandbox",
                        "--disable-dev-shm-usage",
                        "--autoplay-policy=no-user-gesture-required",
                        "--disable-blink-features=AutomationControlled",
                        "--mute-audio",
                        "--user-agent=Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0.0.0 Safari/537.36"
                    }},
                    {"excludeSwitches", {"enable-automation"}},
                    {"perfLoggingPrefs", {
                        {"enableNetwork", true}
                    }}
                }},
                {"goog:loggingPrefs", {
                    {"performance", "ALL"}
                }}
            }}
        }}
    };
    
    json sessionRes = webdriverCommand("/session", "POST", caps);
    if (!sessionRes.contains("value") || !sessionRes["value"].contains("sessionId")) {
        task.errorMessage = "Impossibile connettersi a ChromeDriver locale (Porta 9515).";
        return task;
    }
    std::string sessionId = sessionRes["value"]["sessionId"].get<std::string>();

    try {
        // Navighiamo DIRETTAMENTE alla pagina dell'episodio calcolato, saltando la homepage della serie!
        std::string epUrl = (target.url.find("http") == 0) ? target.url : "https://www.animeworld.ac" + target.url;
        std::cout << "[DEBUG] Navigo direttamente alla pagina dell'episodio: " << epUrl << std::endl;
        webdriverCommand("/session/" + sessionId + "/url", "POST", {{"url", epUrl}});
        
        // Attesa e click su "Player alternativo"
        std::cout << "[DEBUG] Attendo la presenza del pulsante 'Player alternativo'..." << std::endl;
        std::string altBtnId = "";
        for (int i = 0; i < 5; ++i) {
            json altSearch = webdriverCommand("/session/" + sessionId + "/elements", "POST", {
                {"using", "css selector"}, {"value", "#alternative"}
            });
            if (altSearch.contains("value") && altSearch["value"].is_array() && !altSearch["value"].empty()) {
                altBtnId = altSearch["value"][0].begin().value().get<std::string>();
                break;
            }
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

        if (!altBtnId.empty()) {
            std::cout << "[DEBUG] Clicco sul pulsante 'Player alternativo'..." << std::endl;
            webdriverCommand("/session/" + sessionId + "/element/" + altBtnId + "/click", "POST", json::object());
            std::this_thread::sleep_for(std::chrono::seconds(2)); 
        } else {
            std::cout << "[DEBUG WARNING] Pulsante 'Player alternativo' non trovato. Procedo comunque." << std::endl;
        }

        // Attesa caricamento Iframe
        std::cout << "[DEBUG] Attendo che l'iframe del player sia disponibile nel DOM..." << std::endl;
        std::string frameId = "";
        for (int i = 0; i < 10; ++i) {
            json iframeSearch = webdriverCommand("/session/" + sessionId + "/elements", "POST", {
                {"using", "css selector"}, {"value", "iframe#player-iframe"}
            });
            if (iframeSearch.contains("value") && iframeSearch["value"].is_array() && !iframeSearch["value"].empty()) {
                frameId = iframeSearch["value"][0].begin().value().get<std::string>();
                break;
            }
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }

        if (!frameId.empty()) {
            std::cout << "[DEBUG] Eseguo click virtuale sull'iframe per avviare la riproduzione..." << std::endl;
            webdriverCommand("/session/" + sessionId + "/element/" + frameId + "/click", "POST", json::object());
        } else {
            std::cout << "[DEBUG WARNING] Iframe non rilevato in tempo. Avvio comunque lo sniffer..." << std::endl;
        }

        // Fase di Sniffing di Rete (Metodo IDM)
        std::cout << "[DEBUG] Avvio lo sniffer di rete per intercettare il flusso video..." << std::endl;
        std::string dlUrl = "";
        
        for (int i = 0; i < 15; ++i) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
            
            json logRes = webdriverCommand("/session/" + sessionId + "/log", "POST", {
                {"type", "performance"}
            });
            
            if (logRes.contains("value") && logRes["value"].is_array()) {
                for (auto& entry : logRes["value"]) {
                    if (!entry.contains("message")) continue;
                    
                    std::string msgStr = entry["message"].get<std::string>();
                    std::string candidateUrl = extractUrlFromLog(msgStr);
                    if (!candidateUrl.empty()) {
                        dlUrl = candidateUrl;
                        std::cout << "[DEBUG IDM] -> INTERCETTATO VIDEO HTTP: " << dlUrl << std::endl;
                        break;
                    }
                }
            }
            if (!dlUrl.empty()) break;
            std::cout << "[DEBUG] Tentativo " << i + 1 << " - Nessun file .mp4 o .m3u8 intercettato." << std::endl;
        }

        if (dlUrl.empty()) {
            throw std::runtime_error("Timeout: Impossibile intercettare la chiamata di rete del video.");
        }

        task.shouldProcess = true;
        task.videoUrl = dlUrl;
        task.episodeNumber = finalEpNum;
        task.fileName = ScraperUtils::generateFilename(task.videoUrl, series.name, finalEpNum);
        
        std::cout << "[DEBUG] Task completato per il download del file: " << task.fileName << std::endl;

    } catch (const std::exception& e) {
        task.shouldProcess = false;
        task.errorMessage = e.what();
        std::cerr << "[DEBUG EXCEPTION] Scraper fallito con errore: " << e.what() << std::endl;
    }

    webdriverCommand("/session/" + sessionId, "DELETE");
    std::cout << "[DEBUG] --- Fine scraping per: " << series.name << " ---" << std::endl;
    return task;
}

} // namespace Core