#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace Core {

    std::vector<std::string> getRecentLines(std::string_view logPath, int n);

} // namespace Core
