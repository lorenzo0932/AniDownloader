#include "core/Series.hpp"

namespace Core {
    bool Series::operator==(const Series& other) const {
        return name == other.name && seriesPageUrl == other.seriesPageUrl;
    }

    void from_json(const nlohmann::json& j, Series& s) {
        j.at("name").get_to(s.name);
        j.at("service").get_to(s.service); // <--- AGGIUNTO
        j.at("path").get_to(s.path);
        j.at("series_page_url").get_to(s.seriesPageUrl);
        
        // Campi opzionali
        s.episodeListSelector = j.value("episode_list_selector", "");
        s.downloadLinkSelector = j.value("download_link_selector", "");
        s.continueSeries = j.value("continue", true);
        s.passedEpisodes = j.value("passed_episodes", 0);
    }

    void to_json(nlohmann::json& j, const Series& s) {
        j = nlohmann::json{
            {"name", s.name},
            {"service", s.service}, // <--- AGGIUNTO
            {"path", s.path},
            {"continue", s.continueSeries},
            {"passed_episodes", s.passedEpisodes},
            {"series_page_url", s.seriesPageUrl},
            {"episode_list_selector", s.episodeListSelector},
            {"download_link_selector", s.downloadLinkSelector}
        };
    }
}