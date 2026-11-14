#include "core/NameUtils.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>

namespace Core {
    namespace NameUtils {
        namespace {

            constexpr std::size_t kMaxNameCodepoints = 240;

            void appendUtf8(std::string& out, std::uint32_t cp) {
                if (cp <= 0x7F) {
                    out += static_cast<char>(cp);
                } else if (cp <= 0x7FF) {
                    out += static_cast<char>(0xC0 | (cp >> 6));
                    out += static_cast<char>(0x80 | (cp & 0x3F));
                } else if (cp <= 0xFFFF) {
                    if (cp >= 0xD800 && cp <= 0xDFFF)
                        return; // surrogati non validi in UTF-8
                    out += static_cast<char>(0xE0 | (cp >> 12));
                    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                    out += static_cast<char>(0x80 | (cp & 0x3F));
                } else if (cp <= 0x10FFFF) {
                    out += static_cast<char>(0xF0 | (cp >> 18));
                    out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
                    out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                    out += static_cast<char>(0x80 | (cp & 0x3F));
                }
            }

            int hexValue(char c) {
                if (c >= '0' && c <= '9')
                    return c - '0';
                if (c >= 'a' && c <= 'f')
                    return c - 'a' + 10;
                if (c >= 'A' && c <= 'F')
                    return c - 'A' + 10;
                return -1;
            }

            bool isValidUtf8(std::string_view s) {
                std::size_t i = 0;
                while (i < s.size()) {
                    const auto c = static_cast<unsigned char>(s[i]);
                    std::size_t extra = 0;
                    if (c < 0x80) {
                        ++i;
                        continue;
                    }
                    if ((c & 0xE0) == 0xC0) {
                        extra = 1;
                        if (c < 0xC2)
                            return false;
                    } else if ((c & 0xF0) == 0xE0) {
                        extra = 2;
                    } else if ((c & 0xF8) == 0xF0) {
                        extra = 3;
                        if (c > 0xF4)
                            return false;
                    } else {
                        return false;
                    }
                    if (i + extra >= s.size())
                        return false;
                    for (std::size_t j = 1; j <= extra; ++j) {
                        if ((static_cast<unsigned char>(s[i + j]) & 0xC0) != 0x80)
                            return false;
                    }
                    i += extra + 1;
                }
                return true;
            }

            std::string trim(const std::string& value) {
                const auto first = value.find_first_not_of(" \t\r\n");
                if (first == std::string::npos)
                    return {};
                const auto last = value.find_last_not_of(" \t\r\n");
                return value.substr(first, last - first + 1);
            }

            std::string collapseWhitespace(const std::string& value) {
                std::string out;
                out.reserve(value.size());
                bool spazio = false;
                for (const char c : value) {
                    const bool space = c == ' ' || c == '\t' || c == '\r' || c == '\n';
                    if (space) {
                        if (!out.empty() && !spazio) {
                            out += ' ';
                            spazio = true;
                        }
                    } else {
                        out += c;
                        spazio = false;
                    }
                }
                while (!out.empty() && out.back() == ' ')
                    out.pop_back();
                return out;
            }

            std::string upperAscii(std::string value) {
                for (char& c : value)
                    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
                return value;
            }

            bool isReservedDeviceName(const std::string& value) {
                const std::string base = upperAscii(value.substr(0, value.find('.')));
                static const std::array<const char*, 22> kReserved{
                    "CON",  "PRN",  "AUX",  "NUL",  "COM1", "COM2", "COM3", "COM4",
                    "COM5", "COM6", "COM7", "COM8", "COM9", "LPT1", "LPT2", "LPT3",
                    "LPT4", "LPT5", "LPT6", "LPT7", "LPT8", "LPT9"};
                return std::find(kReserved.begin(), kReserved.end(), base) != kReserved.end();
            }

            std::size_t utf8Codepoints(const std::string& value) {
                std::size_t count = 0;
                for (const char c : value) {
                    if ((static_cast<unsigned char>(c) & 0xC0) != 0x80)
                        ++count;
                }
                return count;
            }

            std::string truncateUtf8(const std::string& value, std::size_t maxCp) {
                if (utf8Codepoints(value) <= maxCp)
                    return value;
                std::size_t cp = 0;
                std::size_t i = 0;
                while (i < value.size() && cp < maxCp) {
                    ++i;
                    // Avanza di un code point completo, non di un byte.
                    while (i < value.size() &&
                           (static_cast<unsigned char>(value[i]) & 0xC0) == 0x80)
                        ++i;
                    ++cp;
                }
                return value.substr(0, i);
            }

            const std::unordered_map<std::string, std::string>& namedEntities() {
                static const std::unordered_map<std::string, std::string> table{
                    {"amp", "&"},
                    {"lt", "<"},
                    {"gt", ">"},
                    {"quot", "\""},
                    {"apos", "'"},
                    {"nbsp", " "},
                    {"ndash", "\xE2\x80\x93"},
                    {"mdash", "\xE2\x80\x94"},
                    {"hellip", "\xE2\x80\xA6"},
                    {"lsquo", "\xE2\x80\x98"},
                    {"rsquo", "\xE2\x80\x99"},
                    {"ldquo", "\xE2\x80\x9C"},
                    {"rdquo", "\xE2\x80\x9D"},
                    {"laquo", "\xC2\xAB"},
                    {"raquo", "\xC2\xBB"},
                    {"copy", "\xC2\xA9"},
                    {"reg", "\xC2\xAE"},
                    {"trade", "\xE2\x84\xA2"},
                    {"deg", "\xC2\xB0"},
                    {"middot", "\xC2\xB7"},
                    {"bull", "\xE2\x80\xA2"},
                    {"times", "\xC3\x97"},
                    {"divide", "\xC3\xB7"},
                    {"euro", "\xE2\x82\xAC"},
                    {"pound", "\xC2\xA3"},
                    {"yen", "\xC2\xA5"},
                    {"cent", "\xC2\xA2"},
                    {"sect", "\xC2\xA7"},
                    {"para", "\xC2\xB6"},
                    {"plusmn", "\xC2\xB1"},
                    {"frac12", "\xC2\xBD"},
                    {"frac14", "\xC2\xBC"},
                    {"frac34", "\xC2\xBE"}};
                return table;
            }

        } // namespace

        std::string decodeHtmlEntities(const std::string& value) {
            std::string out;
            out.reserve(value.size());
            for (std::size_t i = 0; i < value.size();) {
                if (value[i] != '&') {
                    out += value[i++];
                    continue;
                }
                const auto semi = value.find(';', i + 1);
                if (semi == std::string::npos || semi - i > 32) {
                    out += value[i++];
                    continue;
                }
                const std::string token = value.substr(i + 1, semi - i - 1);
                bool decoded = false;
                if (!token.empty() && token[0] == '#') {
                    std::uint32_t cp = 0;
                    bool ok = false;
                    if (token.size() > 2 && (token[1] == 'x' || token[1] == 'X')) {
                        ok = true;
                        for (std::size_t k = 2; k < token.size(); ++k) {
                            const int h = hexValue(token[k]);
                            if (h < 0) {
                                ok = false;
                                break;
                            }
                            cp = cp * 16 + static_cast<std::uint32_t>(h);
                        }
                    } else {
                        ok = token.size() > 1;
                        for (std::size_t k = 1; k < token.size(); ++k) {
                            if (!std::isdigit(static_cast<unsigned char>(token[k]))) {
                                ok = false;
                                break;
                            }
                            cp = cp * 10 + static_cast<std::uint32_t>(token[k] - '0');
                        }
                    }
                    if (ok && cp > 0 && cp <= 0x10FFFF) {
                        appendUtf8(out, cp);
                        decoded = true;
                    }
                } else {
                    const auto& table = namedEntities();
                    const auto it = table.find(token);
                    if (it != table.end()) {
                        out += it->second;
                        decoded = true;
                    }
                }
                if (decoded)
                    i = semi + 1;
                else
                    out += value[i++];
            }
            return out;
        }

        std::string decodePercentEncoding(const std::string& value) {
            if (value.find('%') == std::string::npos)
                return value;

            std::string out;
            out.reserve(value.size());
            for (std::size_t i = 0; i < value.size();) {
                if (value[i] != '%') {
                    out += value[i++];
                    continue;
                }
                // %uXXXX (escape JavaScript)
                if (i + 5 < value.size() && (value[i + 1] == 'u' || value[i + 1] == 'U')) {
                    std::uint32_t cp = 0;
                    bool ok = true;
                    for (std::size_t k = 2; k < 6; ++k) {
                        const int h = hexValue(value[i + k]);
                        if (h < 0) {
                            ok = false;
                            break;
                        }
                        cp = cp * 16 + static_cast<std::uint32_t>(h);
                    }
                    if (ok && cp > 0) {
                        appendUtf8(out, cp);
                        i += 6;
                        continue;
                    }
                }
                if (i + 2 < value.size()) {
                    const int hi = hexValue(value[i + 1]);
                    const int lo = hexValue(value[i + 2]);
                    if (hi >= 0 && lo >= 0) {
                        out += static_cast<char>((hi << 4) | lo);
                        i += 3;
                        continue;
                    }
                }
                out += value[i++];
            }
            // Se la decodifica ha prodotto byte non UTF-8, era una percentuale
            // letterale: meglio lasciare il testo originale che corromperlo.
            if (!isValidUtf8(out))
                return value;
            return out;
        }

        std::string normalizeName(const std::string& value) {
            return collapseWhitespace(trim(decodeHtmlEntities(decodePercentEncoding(value))));
        }

        std::string toNtfsSafeName(const std::string& value) {
            std::string out;
            out.reserve(value.size());
            for (const char c : normalizeName(value)) {
                const auto uc = static_cast<unsigned char>(c);
                if (c == ':' || c == '/' || c == '\\') {
                    out += " - ";
                } else if (uc < 0x20 || uc == 0x7F || c == '<' || c == '>' || c == '"' ||
                           c == '|' || c == '?' || c == '*') {
                    out += ' ';
                } else {
                    out += c;
                }
            }

            out = collapseWhitespace(out);
            while (!out.empty() && (out.back() == '.' || out.back() == ' '))
                out.pop_back();
            while (!out.empty() && out.front() == ' ')
                out.erase(out.begin());

            if (out.empty() || out == "." || out == "..")
                out = "Serie";
            if (isReservedDeviceName(out))
                out = "_" + out;

            out = truncateUtf8(out, kMaxNameCodepoints);
            while (!out.empty() && (out.back() == '.' || out.back() == ' '))
                out.pop_back();
            if (out.empty())
                out = "Serie";
            return out;
        }

        bool isNtfsSafe(const std::string& value) {
            if (value.empty() || value == "." || value == "..")
                return false;
            if (value.front() == ' ' || value.back() == '.' || value.back() == ' ')
                return false;
            if (utf8Codepoints(value) > kMaxNameCodepoints)
                return false;
            if (isReservedDeviceName(value))
                return false;
            for (const char c : value) {
                const auto uc = static_cast<unsigned char>(c);
                if (uc < 0x20 || uc == 0x7F)
                    return false;
                if (c == '<' || c == '>' || c == ':' || c == '"' || c == '/' || c == '\\' ||
                    c == '|' || c == '?' || c == '*')
                    return false;
            }
            return true;
        }

    } // namespace NameUtils
} // namespace Core
