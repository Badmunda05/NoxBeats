// Decoding of Pyrogram / Telethon string sessions (the "assistant account" credentials).
#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace bot::mtp {

struct SessionData {
    int dc_id = 0;
    std::uint32_t api_id = 0;     // Pyrogram sessions carry it
    bool test_mode = false;
    std::vector<std::uint8_t> auth_key;   // 256 bytes
    std::int64_t user_id = 0;
    bool is_bot = false;
    std::string ip;               // primary address
    int port = 443;
    std::vector<std::string> fallback_ips;
};

std::optional<SessionData> decode_session(const std::string& s, const std::string& type /* pyrogram|telethon */);
std::vector<std::uint8_t> base64url_decode(std::string s);

}  // namespace bot::mtp
