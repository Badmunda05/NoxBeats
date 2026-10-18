// Port of config.py. Reads .env (if present) then the process environment.
#pragma once
#include <string>
#include <vector>
#include "types.hpp"

namespace bot {

struct Config {
    i64 API_ID = 0;
    std::string API_HASH, BOT_TOKEN;
    std::string MONGO_DB_URI;                 // MongoDB connection string (required)
    std::string DB_PATH = "data/bad.json";   // local fallback file, only used in tests / BOT_MOCK
    i64 LOGGER_ID = 0, OWNER_ID = 0;
    int DURATION_LIMIT = 60 * 60;           // seconds
    int QUEUE_LIMIT = 20, PLAYLIST_LIMIT = 20;
    std::vector<std::string> SESSIONS;      // SESSION / SESSION2 / SESSION3 / STRING_SESSION(S): Pyrogram strings
    std::string SESSION_TYPE = "pyrogram";  // pyrogram | telethon
    std::string BOT_API_URL = "https://api.telegram.org";   // local Bot API server => 2 GB downloads
    std::string API_URL = "https://api.yourdomain.site";   // download API (YouTube -> mp3/mp4); used when API_KEY is set
    std::string API_KEY;
    std::string SUPPORT_CHANNEL = "https://t.me/PBX_UPDATE";
    std::string SOURCE_URL = "https://github.com/Badmunda05/NoxBeats/fork";
    std::string SUPPORT_CHAT = "https://t.me/PBXCHATS";
    bool AUTO_LEAVE = false, AUTO_END = false, THUMB_GEN = true, VIDEO_PLAY = true;
    std::string LANG_CODE = "en";
    std::vector<std::string> COOKIES_URL;
    std::string DEFAULT_THUMB = "https://te.legra.ph/file/3e40a408286d4eda24191.jpg";
    std::string PING_IMG = "https://files.catbox.moe/haagg2.png";
    std::string START_IMG = "https://files.catbox.moe/zvziwk.jpg";

    static Config load();
    // Returns the list of missing required variables (empty = ok).
    std::vector<std::string> check(bool need_sessions = true) const;
};

Config& config();   // global, initialised in main()

}  // namespace bot
