cmake_minimum_required(VERSION 3.17)
project(AniDownloaderCpp VERSION 2.0.0 LANGUAGES CXX)

# --- STANDARD C++ ---
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF) # Disabilita estensioni specifiche (es. GNU), aumenta la portabilità

# --- OTTIMIZZAZIONI C++ AVANZATE ---
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Abilita il database dei comandi di compilazione (utile per IDE come CLion, VS Code, Neovim)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    # 1. Link Time Optimization (LTO / IPO)
    # Fondamentale in C++ per ottimizzare le chiamate tra diversi file .cpp
    include(CheckIPOSupported)
    check_ipo_supported(RESULT lto_supported OUTPUT lto_error)
    if(lto_supported)
        set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
    endif()

    if(MSVC)
        add_compile_options(/O2 /GL /MP) # Ottimizzazione massima e compilazione multi-core
    else()
        # 2. Ottimizzazioni GCC/Clang
        # -O3: Massima ottimizzazione
        # -march=native: Sfrutta istruzioni specifiche della tua CPU (AVX, SSE)
        # -pipe: Usa i pipe invece dei file temporanei (compilazione più veloce)
        # -fno-plt: Riduce l'overhead delle chiamate a librerie esterne
        add_compile_options(-O3 -march=native -pipe -fno-plt)
    endif()
endif()

# --- WARNINGS (Essenziali in C++ per evitare bug silenti) ---
if(MSVC)
    add_compile_options(/W4)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor)
endif()

# --- GESTIONE DIPENDENZE ---
include(FetchContent)

# JSON (Header-only)
FetchContent_Declare(
    json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
)
FetchContent_MakeAvailable(json)

# CPR (C++ Requests - basato su libcurl)
set(BUILD_CPR_TESTS OFF CACHE BOOL "" FORCE)
set(CPR_ENABLE_SSL ON CACHE BOOL "" FORCE)
FetchContent_Declare(
    cpr
    GIT_REPOSITORY https://github.com/libcpr/cpr.git
    GIT_TAG 1.10.5
)
FetchContent_MakeAvailable(cpr)

# cpp-httplib (Header-only HTTP/WebSocket server)
FetchContent_Declare(
    httplib
    URL https://github.com/yhirose/cpp-httplib/archive/refs/tags/v0.18.1.tar.gz
)
FetchContent_MakeAvailable(httplib)

# OpenSSL (per AES-256-GCM e PBKDF2)
find_package(OpenSSL REQUIRED COMPONENTS Crypto)

# Qt6 & Threads
find_package(Threads REQUIRED)
find_package(Qt6 COMPONENTS Widgets REQUIRED)

# --- SORGENTI ---
# Ti consiglio di usare una variabile per i file header e una per i sorgenti
set(HEADERS
    include/gui/MainWindow.hpp
    include/gui/Sidebar.hpp
    include/gui/StatusWidget.hpp
    include/gui/LogsWidget.hpp
    include/gui/SeriesDetailDialog.hpp
    include/gui/DownloadWorker.hpp
    include/gui/Styles.hpp
    include/gui/ScaleHelper.hpp
    include/gui/Widgets.hpp
    include/gui/SeriesManagerWidget.hpp
    include/gui/SeriesEditorDialog.hpp
    include/core/ExecutionEngine.hpp
    include/core/ProcessUtils.hpp
    include/core/MediaProbe.hpp
    include/core/UpdateChecker.hpp
    include/gui/ImageCache.hpp
    include/core/FileUtils.hpp
    include/core/LogUtils.hpp
    include/core/SeriesUtils.hpp
    include/web/CryptoUtils.hpp
    include/web/WebServer.hpp
)

set(SOURCES
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
    src/gui/Sidebar.cpp
    src/gui/StatusWidget.cpp
    src/gui/LogsWidget.cpp
    src/gui/SeriesDetailDialog.cpp
    src/gui/DownloadWorker.cpp
    src/gui/Styles.cpp
    src/gui/ScaleHelper.cpp
    src/gui/Widgets.cpp
    src/gui/SeriesManagerWidget.cpp
    src/gui/SeriesEditorDialog.cpp
    src/core/ExecutionEngine.cpp
    src/core/Logger.cpp
    src/core/ProcessUtils.cpp
    src/core/MediaProbe.cpp
    src/core/UpdateChecker.cpp
    src/core/FileUtils.cpp
    src/core/LogUtils.cpp
    src/core/SeriesUtils.cpp
    src/web/CryptoUtils.cpp
    src/web/WebServer.cpp)

# --- TARGET ---
add_executable(AniDownloader $<$<PLATFORM_ID:Windows>:WIN32> ${SOURCES} ${HEADERS})

# Attiva l'elaborazione automatica di Qt (MOC, RCC, UIC)
set_target_properties(AniDownloader PROPERTIES
    AUTOMOC ON
    AUTORCC ON
    AUTOUIC ON
)

target_include_directories(AniDownloader PRIVATE include src)

target_link_libraries(AniDownloader 
    PRIVATE 
    nlohmann_json::nlohmann_json
    cpr::cpr
    httplib::httplib
    Threads::Threads
    Qt6::Widgets
    OpenSSL::Crypto
)

target_compile_definitions(AniDownloader PRIVATE
    ANIDOWNLOADER_VERSION="${PROJECT_VERSION}"
)

# ─── COPIA FRONTEND IN build/frontend/ ───
add_custom_command(TARGET AniDownloader POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/web/dist"
        "${CMAKE_BINARY_DIR}/frontend"
    COMMENT "Copying frontend to build/frontend/"
)