cmake_minimum_required(VERSION 3.17)
project(AniDownloaderCpp)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# --- GESTIONE DIPENDENZE ---
include(FetchContent)

# 1. JSON Library
FetchContent_Declare(
    json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(json)

# 2. CPR (C++ Requests - Per lo scraping reale)
FetchContent_Declare(
    cpr
    GIT_REPOSITORY https://github.com/libcpr/cpr.git
    GIT_TAG 1.10.5
)
set(BUILD_CPR_TESTS OFF CACHE BOOL "" FORCE) # Velocizza la compilazione
FetchContent_MakeAvailable(cpr)

# 3. Threads
find_package(Threads REQUIRED)

# 4. Qt6
find_package(Qt6 COMPONENTS Widgets REQUIRED)

# --- CONFIGURAZIONE PROGETTO ---
# include_directories(include)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Sorgenti completi
set(CORE_SOURCES
    src/main.cpp
    src/core/SeriesRepository.cpp
    src/core/Series.cpp
    src/core/PlanningService.cpp
    src/core/MediaProcessor.cpp
    src/scrapers/AnimeWScraper.cpp
    src/scrapers/AnimeUScraper.cpp
    src/scrapers/ScraperUtils.cpp
    src/config/AppConfigManager.cpp
    src/config/PathHelper.cpp
    src/gui/MainWindow.cpp
    include/gui/MainWindow.hpp
    src/gui/DownloadWorker.cpp
    include/gui/DownloadWorker.hpp
    src/gui/Styles.cpp
    include/gui/Styles.hpp
    src/gui/Widgets.cpp
    include/gui/Widgets.hpp
    src/gui/SettingsDialog.cpp
    include/gui/SettingsDialog.hpp
    src/gui/SeriesManagerDialog.cpp
    include/gui/SeriesManagerDialog.hpp
    src/gui/SeriesEditorDialog.cpp
    include/gui/SeriesEditorDialog.hpp
    src/core/ExecutionEngine.cpp 
    include/core/ExecutionEngine.hpp
)

add_executable(AniDownloader ${CORE_SOURCES})

# Aggiungi questa riga per rendere visibili sia la cartella include che src (per gli header locali)
target_include_directories(AniDownloader PRIVATE include src)

target_link_libraries(AniDownloader 
    PRIVATE 
    nlohmann_json::nlohmann_json
    cpr::cpr
    Threads::Threads
    Qt6::Widgets
)

install(TARGETS AniDownloader RUNTIME DESTINATION $ENV{HOME}/.local/bin)