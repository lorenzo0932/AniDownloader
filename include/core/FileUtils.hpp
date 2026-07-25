#pragma once

#include <string>
#include <vector>

namespace Core {

int countVideoFiles(const std::string& dirPath);
std::string readNfoDescription(const std::string& seriesPath);
std::string findPosterPath(const std::string& seriesPath);
std::vector<std::pair<std::string, std::string>> listDirectories(const std::string& dirPath);

} // namespace Core
