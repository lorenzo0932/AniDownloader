#pragma once
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace Core {

    struct AlternateSource {
        std::string service;
        std::string seriesPageUrl;
    };

    struct Series {
        std::string name;
        std::string service;
        std::string path;
        bool continueSeries = true;
        bool isHighPriority = false;
        int passedEpisodes = 0;
        std::string seriesPageUrl;
        std::string episodeListSelector;
        std::string downloadLinkSelector;
        std::vector<AlternateSource> alternateSources;

        bool operator==(const Series& other) const;
    };

    void from_json(const nlohmann::json& j, Series& s);
    void to_json(nlohmann::json& j, const Series& s);
}