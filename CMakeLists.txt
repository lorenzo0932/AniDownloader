cmake_minimum_required(VERSION 3.17)
project(AniDownloaderCpp)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

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

# --- CONFIGURAZIONE PROGETTO ---
include_directories(include)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Sorgenti completi
set(CORE_SOURCES
    src/main.cpp
    src/core/SeriesRepository.cpp
    src/core/Series.cpp
    src/core/PlanningService.cpp
    src/core/MediaProcessor.cpp
    src/core/scrapers/AnimeWScraper.cpp
    src/core/scrapers/AnimeUScraper.cpp
    src/core/scrapers/ScraperUtils.cpp
    src/config/AppConfigManager.cpp
    src/config/PathHelper.cpp
)

add_executable(AniDownloader ${CORE_SOURCES})

# Link alle librerie (CPR tira dentro anche curl e openssl automaticamente)
target_link_libraries(AniDownloader 
    PRIVATE 
        nlohmann_json::nlohmann_json
        cpr::cpr
        Threads::Threads
)