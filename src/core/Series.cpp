#include "core/Series.hpp"

namespace Core {
    bool Series::operator==(const Series& other) const {
        return name == other.name && seriesPageUrl == other.seriesPageUrl;
    }

    void from_json(const nlohmann::json& j, Series& s) {
        j.at("name").get_to(s.name);
        j.at("service").get_to(s.service);
        j.at("path").get_to(s.path);
        j.at("series_page_url").get_to(s.seriesPageUrl);
        
        // Campi opzionali
        s.episodeListSelector = j.value("episode_list_selector", "");
        s.downloadLinkSelector = j.value("download_link_selector", "");
        s.continueSeries = j.value("continue", true);
        s.isHighPriority = j.value("is_high_priority", false);
        s.passedEpisodes = j.value("passed_episodes", 0);

        // Fonti alternate opzionali
        s.alternateSources.clear();
        if (j.contains("alternate_sources") && j["alternate_sources"].is_array()) {
            for (const auto& src : j["alternate_sources"]) {
                AlternateSource as;
                as.service = src.value("service", "");
                as.seriesPageUrl = src.value("series_page_url", "");
                if (!as.service.empty() && !as.seriesPageUrl.empty()) {
                    s.alternateSources.push_back(as);
                }
            }
        }
    }

    void to_json(nlohmann::json& j, const Series& s) {
        j = nlohmann::json{
            {"name", s.name},
            {"service", s.service},
            {"path", s.path},
            {"continue", s.continueSeries},
            {"is_high_priority", s.isHighPriority},
            {"passed_episodes", s.passedEpisodes},
            {"series_page_url", s.seriesPageUrl},
            {"episode_list_selector", s.episodeListSelector},
            {"download_link_selector", s.downloadLinkSelector}
        };

        if (!s.alternateSources.empty()) {
            nlohmann::json arr = nlohmann::json::array();
            for (const auto& src : s.alternateSources) {
                arr.push_back({{"service", src.service}, {"series_page_url", src.seriesPageUrl}});
            }
            j["alternate_sources"] = arr;
        }
    }
}