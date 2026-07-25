#include "core/SeriesUtils.hpp"
#include <algorithm>
#include <cctype>

namespace Core {

static std::string toLower(const std::string& s) {
    std::string r = s;
    std::transform(r.begin(), r.end(), r.begin(), ::tolower);
    return r;
}

void sortSeries(nlohmann::json& data, const std::string& field, bool desc) {
    static const std::vector<std::string> validFields = {
        "name", "local_episode_count", "continue"
    };
    if (std::find(validFields.begin(), validFields.end(), field) == validFields.end())
        return;

    std::sort(data.begin(), data.end(), [&](const nlohmann::json& a, const nlohmann::json& b) {
        auto getVal = [&](const nlohmann::json& j) -> nlohmann::json {
            if (!j.is_object()) return nlohmann::json("");
            if (field == "name") {
                auto n = j.value("name", j.value("title", nlohmann::json("")));
                if (n.is_null()) return nlohmann::json("");
                return nlohmann::json(toLower(n.get<std::string>()));
            }
            auto v = j.value(field, nlohmann::json());
            if (v.is_null()) return nlohmann::json("");
            if (v.is_boolean()) return nlohmann::json(v.get<bool>() ? "1" : "0");
            return v;
        };

        auto va = getVal(a);
        auto vb = getVal(b);

        bool less = false;
        if (va.is_number() && vb.is_number()) {
            less = va.get<double>() < vb.get<double>();
        } else {
            auto toStr = [](const nlohmann::json& v) -> std::string {
                if (v.is_string()) return v.get<std::string>();
                if (v.is_boolean()) return v.get<bool>() ? "1" : "0";
                if (v.is_number()) return std::to_string(v.get<double>());
                return "";
            };
            less = toStr(va) < toStr(vb);
        }

        return desc ? !less : less;
    });
}

}
