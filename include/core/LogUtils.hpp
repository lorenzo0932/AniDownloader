#pragma once

#include <string>
#include <vector>

namespace Core {

std::vector<std::string> getRecentLines(const std::string& logPath, int n);

} // namespace Core
