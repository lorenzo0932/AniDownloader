#pragma once
#include <nlohmann/json.hpp>
#include <string>

namespace Core {

    void sortSeries(nlohmann::json& data, const std::string& field, bool desc);

}
