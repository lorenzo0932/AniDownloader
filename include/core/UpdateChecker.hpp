#pragma once
#include <functional>
#include <string>

namespace Core {

    struct UpdateInfo {
        bool updateAvailable = false;
        std::string latestVersion;
        std::string downloadUrl;
    };

    class UpdateChecker {
      public:
        using Callback = std::function<void(const UpdateInfo&)>;

        static void checkForUpdates(const std::string& currentVersion, Callback callback);
        static int compareVersions(const std::string& a, const std::string& b);
    };

} // namespace Core
