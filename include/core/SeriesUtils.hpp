#pragma once
#include <string>
#include <nlohmann/json.hpp>

namespace Core {

void sortSeries(nlohmann::json& data, const std::string& field, bool desc);

}
