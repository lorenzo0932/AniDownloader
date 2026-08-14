// Unit test offline dei parser degli scraper (nessuna rete: fixture in
// tests/fixtures/). Eseguire con: ctest --test-dir build  (oppure ./build/test_scraper)
#include "core/Logger.hpp"
#include "scrapers/AnimeUScraper.hpp"
#include "scrapers/AnimeWScraper.hpp"
#include "scrapers/ScraperUtils.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>

static int g_failures = 0;

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            ++g_failures;                                                                          \
            std::cerr << "FAIL: " << #cond << " (riga " << __LINE__ << ")\n";                      \
        }                                                                                          \
    } while (0)

static std::string readFixture(const std::string& name) {
    std::ifstream f(std::filesystem::path(FIXTURES_DIR) / name);
    if (!f)
        return "";
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

static void testAnimeWParseSeriesPage() {
    using Core::AnimeWScraper;

    auto eps = AnimeWScraper::parseSeriesPage(readFixture("animew_page.html"));
    // 5 tag con data-episode-num, ma 1 senza href -> 4 candidati, ordinati per numero
    CHECK(eps.size() == 4);
    if (eps.size() == 4) {
        CHECK(eps[0].episodeNumber == 1);
        CHECK(eps[0].episodeUrl == "/play/guarding.xxxxx/zzz112");
        CHECK(eps[1].episodeNumber == 2);
        CHECK(eps[1].episodeUrl == "/play/guarding.xxxxx/zzz115");
        CHECK(eps[2].episodeNumber == 3);
        CHECK(eps[2].episodeUrl == "/play/guarding.xxxxx/zzz114");
        CHECK(eps[3].episodeNumber == 5);
        CHECK(eps[3].episodeUrl == "/play/guarding.xxxxx/zzz111");
    }

    // Struttura alterata (niente data-episode-num) -> lista vuota, nessun throw
    CHECK(AnimeWScraper::parseSeriesPage(readFixture("animew_no_episodes.html")).empty());
    CHECK(AnimeWScraper::parseSeriesPage("").empty());
    CHECK(AnimeWScraper::parseSeriesPage("<html><body>nessun episodio</body></html>").empty());
}

static void testAnimeWParseEpisodeInfo() {
    using Core::AnimeWScraper;

    CHECK(AnimeWScraper::parseEpisodeInfo(readFixture("animew_episode_info.json")) ==
          "https://srv18-tsurukusa.sweetpixel.org/DDL/ANIME/Guarding/Guarding_Ep_01_SUB_ITA.mp4");

    // {"error": true} -> grabber vuoto (episodio non disponibile)
    CHECK(AnimeWScraper::parseEpisodeInfo(readFixture("animew_episode_info_error.json")).empty());

    // Body non JSON / vuoto -> nessun throw, grabber vuoto
    CHECK(AnimeWScraper::parseEpisodeInfo("non è json").empty());
    CHECK(AnimeWScraper::parseEpisodeInfo("").empty());
    CHECK(AnimeWScraper::parseEpisodeInfo("{}").empty());
}

static void testAnimeUParseSeriesPage() {
    using Core::AnimeUScraper;

    auto eps = AnimeUScraper::parseSeriesPage(readFixture("animeu_page.html"));
    // 4 episode-item, ordinati per numero episodio
    CHECK(eps.size() == 4);
    if (eps.size() == 4) {
        CHECK(eps[0].episodeNumber == 1);
        CHECK(eps[0].episodeUrl == "/watch/guarding/4252");
        CHECK(eps[1].episodeNumber == 2);
        CHECK(eps[1].episodeUrl == "/watch/guarding/4253");
        CHECK(eps[2].episodeNumber == 3);
        CHECK(eps[2].episodeUrl == "/watch/guarding/4254");
        CHECK(eps[3].episodeNumber == 5);
        CHECK(eps[3].episodeUrl == "/watch/guarding/4256");
    }

    CHECK(AnimeUScraper::parseSeriesPage("").empty());
    CHECK(AnimeUScraper::parseSeriesPage("<html><body>niente</body></html>").empty());
}

static void testAnimeUParseEpisodeAndEmbed() {
    using Core::AnimeUScraper;

    CHECK(AnimeUScraper::parseEpisodePage(readFixture("animeu_episode_page.html")) ==
          "https://au-player.example.com/embed/4252");
    // Pagina episodio senza iframe id="embed" -> vuoto
    CHECK(AnimeUScraper::parseEpisodePage(readFixture("animeu_episode_no_iframe.html")).empty());
    CHECK(AnimeUScraper::parseEpisodePage("").empty());

    CHECK(AnimeUScraper::parseEmbedPage(readFixture("animeu_embed_page.html")) ==
          "https://cdn.au-player.example.com/dl/guarding/Guarding_Ep_01.mp4");
    CHECK(AnimeUScraper::parseEmbedPage("<script>var x = 1;</script>").empty());
    CHECK(AnimeUScraper::parseEmbedPage("").empty());
}

static void testExtractSeriesTitle() {
    using Core::ScraperUtils;

    // Formato reale campionato da animeworld.ac (14 ago 2026):
    // "<Titolo> Episodio N Streaming & Download SUB ITA - AnimeWorld"
    CHECK(ScraperUtils::extractSeriesTitle(
              "<html><head><title>Smoking Behind the Supermarket with You Episodio 1 "
              "Streaming & Download SUB ITA - AnimeWorld</title></head></html>") ==
          "Smoking Behind the Supermarket with You");

    // Prefissi legacy "<Sito> - <Titolo>"
    CHECK(ScraperUtils::extractSeriesTitle("<title>AnimeWorld - Naruto</title>") == "Naruto");
    CHECK(ScraperUtils::extractSeriesTitle("<title>AnimeUnity - One Piece</title>") == "One Piece");
    CHECK(ScraperUtils::extractSeriesTitle("<title>AnimeWorld - Guarding (ITA)</title>") ==
          "Guarding (ITA)");

    // Suffissi Episodio/Episode (inglese e italiano)
    CHECK(ScraperUtils::extractSeriesTitle("<title>Naruto Episodio 12</title>") == "Naruto");
    CHECK(ScraperUtils::extractSeriesTitle("<title>One Piece Episode 5</title>") == "One Piece");

    // Nessun <title> o vuoto -> stringa vuota
    CHECK(ScraperUtils::extractSeriesTitle("<html><body></body></html>").empty());
    CHECK(ScraperUtils::extractSeriesTitle("").empty());
    // Titolo solo spazi dopo la pulizia -> vuoto
    CHECK(ScraperUtils::extractSeriesTitle("<title>AnimeWorld -   </title>").empty());
}

int main() {
    testAnimeWParseSeriesPage();
    testAnimeWParseEpisodeInfo();
    testAnimeUParseSeriesPage();
    testAnimeUParseEpisodeAndEmbed();
    testExtractSeriesTitle();

    if (g_failures == 0) {
        std::cout << "test_scraper: tutti i test superati\n";
        return 0;
    }
    std::cerr << "test_scraper: " << g_failures << " fallimenti\n";
    return 1;
}
