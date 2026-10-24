#include "config.hpp"
#include <cstdlib>
#include <fstream>
#include <map>
#include "util.hpp"

namespace bot {
namespace {
std::map<std::string, std::string> read_dotenv(const std::string& path) {
    std::map<std::string, std::string> m;
    std::ifstream f(path);
    std::string line;
    while (std::getline(f, line)) {
        line = util::trim(line);
        if (line.empty() || line[0] == '#') continue;
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string k = util::trim(line.substr(0, eq)), v = util::trim(line.substr(eq + 1));
        if (v.size() >= 2 && ((v.front() == '"' && v.back() == '"') || (v.front() == '\'' && v.back() == '\'')))
            v = v.substr(1, v.size() - 2);
        m[k] = v;
    }
    return m;
}
}  // namespace

Config Config::load() {
    auto env = read_dotenv(".env");
    auto get = [&](const char* k, const std::string& def = "") -> std::string {
        if (const char* e = std::getenv(k)) return e;   // a real environment variable wins over .env
        auto it = env.find(k);
        return it != env.end() ? it->second : def;
    };
    auto geti = [&](const char* k, long long def) -> long long {
        std::string v = get(k);
        try { return v.empty() ? def : std::stoll(v); } catch (...) { return def; }
    };
    auto getb = [&](const char* k, bool def) { return util::lower(get(k, def ? "true" : "false")) == "true"; };

    Config c;
    c.API_ID = geti("API_ID", 0);
    c.API_HASH = get("API_HASH");
    c.BOT_TOKEN = get("BOT_TOKEN", get("TOKEN"));
    c.DB_PATH = get("DB_PATH", c.DB_PATH);
    c.MONGO_DB_URI = get("MONGO_DB_URI", get("MONGO_URL", get("MONGO_DB_URL")));
    c.LOGGER_ID = geti("LOGGER_ID", 0);
    c.OWNER_ID = geti("OWNER_ID", 0);
    c.DURATION_LIMIT = static_cast<int>(geti("DURATION_LIMIT", 60) * 60);
    c.QUEUE_LIMIT = static_cast<int>(geti("QUEUE_LIMIT", 20));
    c.PLAYLIST_LIMIT = static_cast<int>(geti("PLAYLIST_LIMIT", 20));
    // SESSION, SESSION2 ... SESSION10 (+ STRING_SESSION(S)); more assistants spread the voice chats over more accounts
    std::vector<std::string> session_keys = {"SESSION", "STRING_SESSION", "STRING_SESSIONS"};
    for (int i = 2; i <= Config::kMaxSessions; ++i) session_keys.push_back("SESSION" + std::to_string(i));
    for (const auto& k : session_keys)
        for (const auto& s : util::split_ws(get(k.c_str())))   // strings never contain spaces
            if (!util::contains(c.SESSIONS, s)) c.SESSIONS.push_back(s);
    if (static_cast<int>(c.SESSIONS.size()) > Config::kMaxSessions) c.SESSIONS.resize(Config::kMaxSessions);
    c.SESSION_TYPE = util::lower(get("SESSION_TYPE", c.SESSION_TYPE));
    c.BOT_API_URL = get("BOT_API_URL", c.BOT_API_URL);
    c.API_URL = get("API_URL", c.API_URL);
    while (!c.API_URL.empty() && c.API_URL.back() == '/') c.API_URL.pop_back();
    c.API_KEY = get("API_KEY");
    c.SUPPORT_CHANNEL = get("SUPPORT_CHANNEL", c.SUPPORT_CHANNEL);
    c.SUPPORT_CHAT = get("SUPPORT_CHAT", c.SUPPORT_CHAT);
    c.SOURCE_URL = get("SOURCE_URL", c.SOURCE_URL);
    c.AUTO_LEAVE = getb("AUTO_LEAVE", false);
    c.AUTO_END = getb("AUTO_END", false);
    c.YTDLP_UPDATE = getb("YTDLP_UPDATE", true);
    c.THUMB_GEN = getb("THUMB_GEN", true);
    c.VIDEO_PLAY = getb("VIDEO_PLAY", true);
    c.LANG_CODE = get("LANG_CODE", "en");
    {   // default: API first when a download API is configured, otherwise cookies/yt-dlp
        const bool api_set = !get("API_KEY").empty();
        c.USE_COOKIES = getb("USE_COOKIES", !api_set);
    }
    c.SPOTIFY_CLIENT_ID = get("SPOTIFY_CLIENT_ID");
    c.SPOTIFY_CLIENT_SECRET = get("SPOTIFY_CLIENT_SECRET");
    for (const auto& u : util::split_ws(get("COOKIES_URL")))
        if (u.find("batbin.me") != std::string::npos) c.COOKIES_URL.push_back(u);
    c.DEFAULT_THUMB = get("DEFAULT_THUMB", c.DEFAULT_THUMB);
    c.PING_IMG = get("PING_IMG", c.PING_IMG);
    c.START_IMG = get("START_IMG", c.START_IMG);
    return c;
}

std::vector<std::string> Config::check(bool need_sessions) const {
    std::vector<std::string> miss;
    if (!API_ID) miss.push_back("API_ID");
    if (API_HASH.empty()) miss.push_back("API_HASH");
    if (BOT_TOKEN.empty()) miss.push_back("BOT_TOKEN");
    if (!LOGGER_ID) miss.push_back("LOGGER_ID");
    if (!OWNER_ID) miss.push_back("OWNER_ID");
    if (need_sessions && SESSIONS.empty()) miss.push_back("SESSION");
    if (need_sessions && MONGO_DB_URI.empty()) miss.push_back("MONGO_DB_URI");
    return miss;
}

Config& config() {
    static Config c;
    return c;
}

}  // namespace bot
