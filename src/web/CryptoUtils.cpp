#include "web/CryptoUtils.hpp"
#include <openssl/hmac.h>
#include <openssl/rand.h>
#include <cstring>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <nlohmann/json.hpp>

namespace Web {

std::string bytesToHex(const std::vector<uint8_t>& data) {
    std::ostringstream oss;
    for (auto b : data)
        oss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(b);
    return oss.str();
}

std::vector<uint8_t> hexToBytes(const std::string& hex) {
    std::vector<uint8_t> bytes;
    for (size_t i = 0; i < hex.length(); i += 2) {
        auto byte = static_cast<uint8_t>(std::stoi(hex.substr(i, 2), nullptr, 16));
        bytes.push_back(byte);
    }
    return bytes;
}

static const std::string B64U = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

std::string base64UrlEncode(const std::vector<uint8_t>& data) {
    std::string out;
    int val = 0, valb = -6;
    for (auto c : data) {
        val = (val << 8) + c;
        valb += 8;
        while (valb >= 0) {
            out.push_back(B64U[(val >> valb) & 0x3F]);
            valb -= 6;
        }
    }
    if (valb > -6) out.push_back(B64U[((val << 8) >> (valb + 8)) & 0x3F]);
    return out;
}

std::vector<uint8_t> base64UrlDecode(const std::string& input) {
    std::vector<uint8_t> out;
    std::vector<int> T(256, -1);
    for (int i = 0; i < 64; i++) T[B64U[i]] = i;

    int val = 0, valb = -8;
    for (auto c : input) {
        if (T[static_cast<unsigned char>(c)] == -1) break;
        val = (val << 6) + T[static_cast<unsigned char>(c)];
        valb += 6;
        if (valb >= 0) {
            out.push_back(static_cast<uint8_t>((val >> valb) & 0xFF));
            valb -= 8;
        }
    }
    return out;
}

std::string createJwt(const std::string& subject,
                       const std::string& secret,
                       std::chrono::seconds expiry) {
    auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());

    nlohmann::json header = {{"alg", "HS256"}, {"typ", "JWT"}};
    nlohmann::json payload = {
        {"sub", subject},
        {"iat", now},
        {"exp", now + expiry.count()}
    };

    auto hdrStr = header.dump();
    std::vector<uint8_t> hdrVec(hdrStr.begin(), hdrStr.end());
    std::string headerB64 = base64UrlEncode(hdrVec);
    
    auto pldStr = payload.dump();
    std::vector<uint8_t> pldVec(pldStr.begin(), pldStr.end());
    std::string payloadB64 = base64UrlEncode(pldVec);

    std::string signingInput = headerB64 + "." + payloadB64;

    unsigned char hmacResult[EVP_MAX_MD_SIZE];
    unsigned int hmacLen = 0;
    HMAC(EVP_sha256(), secret.data(), static_cast<int>(secret.size()),
         reinterpret_cast<const unsigned char*>(signingInput.data()),
         signingInput.size(), hmacResult, &hmacLen);

    std::string sigB64 = base64UrlEncode(
        std::vector<uint8_t>(hmacResult, hmacResult + hmacLen));

    return signingInput + "." + sigB64;
}

bool verifyJwt(const std::string& token,
               const std::string& secret,
               std::string& outSubject) {
    auto firstDot = token.find('.');
    auto lastDot = token.rfind('.');
    if (firstDot == std::string::npos || lastDot == std::string::npos || firstDot == lastDot)
        return false;

    std::string headerB64 = token.substr(0, firstDot);
    std::string payloadB64 = token.substr(firstDot + 1, lastDot - firstDot - 1);
    std::string sigB64 = token.substr(lastDot + 1);
    std::string signingInput = headerB64 + "." + payloadB64;

    auto expectedSig = base64UrlDecode(sigB64);
    unsigned char hmacResult[EVP_MAX_MD_SIZE];
    unsigned int hmacLen = 0;
    HMAC(EVP_sha256(), secret.data(), static_cast<int>(secret.size()),
         reinterpret_cast<const unsigned char*>(signingInput.data()),
         signingInput.size(), hmacResult, &hmacLen);

    if (static_cast<unsigned int>(expectedSig.size()) != hmacLen)
        return false;
    if (CRYPTO_memcmp(expectedSig.data(), hmacResult, hmacLen) != 0)
        return false;

    auto payloadBytes = base64UrlDecode(payloadB64);
    std::string payloadStr(payloadBytes.begin(), payloadBytes.end());

    try {
        auto json = nlohmann::json::parse(payloadStr);
        auto now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        if (json.contains("exp") && json["exp"].get<int64_t>() < now)
            return false;
        outSubject = json.value("sub", "");
        return !outSubject.empty();
    } catch (...) {
        return false;
    }
}

} // namespace Web
