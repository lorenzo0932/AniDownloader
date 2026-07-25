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

# Threads
find_package(Threads REQUIRED)

# --- SORGENTI ---
set(HEADERS
    include/core/ExecutionEngine.hpp
    include/core/ProcessUtils.hpp
    include/core/MediaProbe.hpp
    include/core/UpdateChecker.hpp
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

target_include_directories(AniDownloader PRIVATE include src)

target_link_libraries(AniDownloader 
    PRIVATE 
    nlohmann_json::nlohmann_json
    cpr::cpr
    httplib::httplib
    Threads::Threads
    OpenSSL::Crypto
)

target_compile_definitions(AniDownloader PRIVATE
    ANIDOWNLOADER_VERSION="${PROJECT_VERSION}"
)

# ─── TARGET TRIPLE PER SIDECAR TAURI ───
if(WIN32)
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(TAURI_TARGET "x86_64-pc-windows-msvc")
    else()
        set(TAURI_TARGET "i686-pc-windows-msvc")
    endif()
elseif(APPLE)
    if(CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64")
        set(TAURI_TARGET "aarch64-apple-darwin")
    else()
        set(TAURI_TARGET "x86_64-apple-darwin")
    endif()
else()
    if(CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64")
        set(TAURI_TARGET "aarch64-unknown-linux-gnu")
    else()
        set(TAURI_TARGET "x86_64-unknown-linux-gnu")
    endif()
endif()

# ─── COPIA FRONTEND IN build/frontend/ (per modalità --web standalone) ───
add_custom_command(TARGET AniDownloader POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        "${CMAKE_SOURCE_DIR}/web/dist"
        "${CMAKE_BINARY_DIR}/frontend"
    COMMENT "Copying frontend to build/frontend/"
)

# ─── COPIA SIDECAR IN src-tauri/binaries/ (per Tauri) ───
add_custom_command(TARGET AniDownloader POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "${CMAKE_SOURCE_DIR}/src-tauri/binaries"
    COMMAND ${CMAKE_COMMAND} -E copy
        "$<TARGET_FILE:AniDownloader>"
        "${CMAKE_SOURCE_DIR}/src-tauri/binaries/AniDownloader-${TAURI_TARGET}$<$<PLATFORM_ID:Windows>:.exe>"
    COMMENT "Copying sidecar to src-tauri/binaries/AniDownloader-${TAURI_TARGET}"
)