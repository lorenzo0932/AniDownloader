#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <array>
#include <chrono>

namespace Web {

constexpr int KEY_SIZE = 32;

std::string bytesToHex(const std::vector<uint8_t>& data);
std::vector<uint8_t> hexToBytes(const std::string& hex);

std::string base64UrlEncode(const std::vector<uint8_t>& data);
std::vector<uint8_t> base64UrlDecode(const std::string& input);

std::string createJwt(const std::string& subject,
                       const std::string& secret,
                       std::chrono::seconds expiry = std::chrono::hours(24));

bool verifyJwt(const std::string& token,
               const std::string& secret,
               std::string& outSubject);

} // namespace Web
