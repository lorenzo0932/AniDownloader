cmake_minimum_required(VERSION 3.17)
project(AniDownloaderCpp VERSION 2.0.1 LANGUAGES CXX)

# --- STANDARD C++ ---
set(CMAKE_CXX_STANDARD 20)
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

# Python (per embed_web.py — cross-platform, fallback a python3/python)
find_package(Python3 QUIET COMPONENTS Interpreter)
if(NOT Python3_FOUND)
    find_program(Python3_EXECUTABLE python3 python)
    if(NOT Python3_EXECUTABLE)
        message(WARNING "Python3 not found — frontend embed will be skipped at build time")
    endif()
endif()

# JSON (Header-only)
FetchContent_Declare(
    json
    URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
)
FetchContent_MakeAvailable(json)

# CPR (C++ Requests - basato su libcurl)
set(BUILD_CPR_TESTS OFF CACHE BOOL "" FORCE)
set(CPR_ENABLE_SSL ON CACHE BOOL "" FORCE)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
# Usa curl di sistema se disponibile (vcpkg, brew, apt) per evitare
# bug di build da fonte su Windows (MSVC access violation in __create_def)
find_package(CURL QUIET)
if(CURL_FOUND)
    set(CPR_FORCE_USE_SYSTEM_CURL ON CACHE BOOL "" FORCE)
    message(STATUS "Using system curl: ${CURL_LIBRARIES}")
else()
    message(STATUS "System curl not found, CPR will fetch its own")
endif()
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

# Threads
find_package(Threads REQUIRED)

# --- EMBEDDED FRONTEND ---
set(EMBEDDED_HPP ${CMAKE_SOURCE_DIR}/include/web/embedded_web.hpp)
set(EMBEDDED_CPP ${CMAKE_SOURCE_DIR}/src/web/embedded_web_data.cpp)

# --- SORGENTI ---
set(HEADERS
    include/core/ExecutionEngine.hpp
    include/core/ProcessUtils.hpp
    include/core/MediaProbe.hpp
    include/core/UpdateChecker.hpp
    include/core/FileUtils.hpp
    include/core/LogUtils.hpp
    include/core/SeriesUtils.hpp
    include/web/WebServer.hpp
    ${EMBEDDED_HPP}
)

file(GLOB_RECURSE WEB_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_SOURCE_DIR}/web/src/*
    ${CMAKE_SOURCE_DIR}/web/*.json
    ${CMAKE_SOURCE_DIR}/web/*.html
)

if(Python3_FOUND OR Python3_EXECUTABLE)
    add_custom_command(
        OUTPUT ${EMBEDDED_CPP} ${EMBEDDED_HPP}
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/scripts/embed_web.py
            ${CMAKE_SOURCE_DIR}/web ${CMAKE_SOURCE_DIR}/web/dist ${EMBEDDED_HPP}
        DEPENDS
            ${CMAKE_SOURCE_DIR}/scripts/embed_web.py
            ${WEB_SOURCES}
        COMMENT "Building Svelte frontend and embedding into C++ binary"
    )
else()
    add_custom_command(
        OUTPUT ${EMBEDDED_CPP} ${EMBEDDED_HPP}
        COMMAND ${CMAKE_COMMAND} -E echo "Skipping frontend embed — Python3 not found"
        COMMAND ${CMAKE_COMMAND} -E touch ${EMBEDDED_CPP}
        COMMENT "Skipping frontend embed (Python3 not available)"
    )
endif()

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
    src/core/InstanceLock.cpp
    src/core/Logger.cpp
    src/core/ProcessUtils.cpp
    src/core/MediaProbe.cpp
    src/core/UpdateChecker.cpp
    src/core/FileUtils.cpp
    src/core/LogUtils.cpp
    src/core/SeriesUtils.cpp
    src/web/WebServer.cpp
    ${EMBEDDED_CPP})

# --- TARGET ---
add_executable(AniDownloader ${SOURCES} ${HEADERS})

target_include_directories(AniDownloader PRIVATE include src)

target_link_libraries(AniDownloader 
    PRIVATE 
    nlohmann_json::nlohmann_json
    cpr::cpr
    httplib::httplib
    Threads::Threads
)

target_compile_definitions(AniDownloader PRIVATE
    ANIDOWNLOADER_VERSION="${PROJECT_VERSION}"
)

# ─── TEST ───
enable_testing()
add_executable(test_core
    tests/test_core.cpp
    src/core/InstanceLock.cpp
    src/core/ProcessUtils.cpp
    src/core/UpdateChecker.cpp
    src/core/Logger.cpp
    src/core/Series.cpp
    src/core/SeriesRepository.cpp
    src/scrapers/ScraperUtils.cpp
)
target_include_directories(test_core PRIVATE include)
target_link_libraries(test_core PRIVATE
    nlohmann_json::nlohmann_json
    cpr::cpr
    Threads::Threads
)
add_test(NAME test_core COMMAND test_core)

# ─── TEST MEDIA (pipeline offline: download aria2 + conversione H265) ───
# Serve un server HTTP locale (httplib): aria2 non supporta file:// in ogni build.
add_executable(test_media
    tests/test_media.cpp
    src/core/MediaProcessor.cpp
    src/core/MediaProbe.cpp
    src/core/ProcessUtils.cpp
    src/core/FileUtils.cpp
    src/scrapers/ScraperUtils.cpp
    src/core/Logger.cpp
    src/config/AppConfigManager.cpp
    src/config/PathHelper.cpp
    src/core/Series.cpp
)
target_include_directories(test_media PRIVATE include)
target_link_libraries(test_media PRIVATE
    nlohmann_json::nlohmann_json
    cpr::cpr
    httplib::httplib
    Threads::Threads
)
add_test(NAME test_media COMMAND test_media)

# ─── TEST SCRAPER (parser offline: fixture HTML/JSON in tests/fixtures/) ───
add_executable(test_scraper
    tests/test_scraper.cpp
    src/scrapers/AnimeWScraper.cpp
    src/scrapers/AnimeUScraper.cpp
    src/scrapers/ScraperUtils.cpp
    src/core/Logger.cpp
    src/core/Series.cpp
)
target_include_directories(test_scraper PRIVATE include)
target_compile_definitions(test_scraper PRIVATE
    FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures"
)
target_link_libraries(test_scraper PRIVATE
    nlohmann_json::nlohmann_json
    cpr::cpr
    Threads::Threads
)
add_test(NAME test_scraper COMMAND test_scraper)

# ─── SMOKE E2E (solo Linux: script POSIX con curl/jq/python3) ───
if(UNIX AND NOT APPLE)
    add_test(NAME smoke_e2e COMMAND bash ${CMAKE_SOURCE_DIR}/tests/e2e/smoke.sh)
endif()

# ─── TARGET TRIPLE PER SIDECAR TAURI ───
if(WIN32)
    if(CMAKE_SIZEOF_VOID_P EQUAL 8)
        set(TAURI_TARGET "x86_64-pc-windows-msvc")
    else()
        set(TAURI_TARGET "i686-pc-windows-msvc")
    endif()
elseif(APPLE)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
        set(TAURI_TARGET "aarch64-apple-darwin")
    else()
        set(TAURI_TARGET "x86_64-apple-darwin")
    endif()
else()
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "^(aarch64|arm64)$")
        set(TAURI_TARGET "aarch64-unknown-linux-gnu")
    else()
        set(TAURI_TARGET "x86_64-unknown-linux-gnu")
    endif()
endif()

# ─── COPIA SIDECAR IN src-tauri/binaries/ (per Tauri bundle) ───
add_custom_command(TARGET AniDownloader POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "${CMAKE_SOURCE_DIR}/src-tauri/binaries"
    COMMAND ${CMAKE_COMMAND} -E copy
        "$<TARGET_FILE:AniDownloader>"
        "${CMAKE_SOURCE_DIR}/src-tauri/binaries/anidownloaderd-${TAURI_TARGET}$<$<PLATFORM_ID:Windows>:.exe>"
    COMMENT "Copying sidecar to src-tauri/binaries/"
)

# ─── COPIA ANCHE IN src-tauri/target/ per sviluppo (stessa dir del Rust binary) ───
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    set(CARGO_PROFILE "release")
else()
    set(CARGO_PROFILE "debug")
endif()
add_custom_command(TARGET AniDownloader POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "${CMAKE_SOURCE_DIR}/src-tauri/target/${CARGO_PROFILE}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "$<TARGET_FILE:AniDownloader>"
        "${CMAKE_SOURCE_DIR}/src-tauri/target/${CARGO_PROFILE}/anidownloaderd-${TAURI_TARGET}$<$<PLATFORM_ID:Windows>:.exe>"
    COMMENT "Copying sidecar to src-tauri/target/${CARGO_PROFILE}/"
)