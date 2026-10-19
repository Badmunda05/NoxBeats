#include "yt.hpp"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <nlohmann/json.hpp>
#include <atomic>
#include <functional>
#include "../core/config.hpp"
#include "../core/log.hpp"
#include "../net/http.hpp"
#include "../core/util.hpp"

namespace bot {
using nlohmann::json;
namespace fs = std::filesystem;

YouTube::YouTube()
    : regex_(R"((https?://)?(www\.|m\.|music\.)?(youtube\.com/(watch\?v=|shorts/|playlist\?list=)|youtu\.be/)([A-Za-z0-9_-]{11}|PL[A-Za-z0-9_-]+)([&?][^\s]*)?)"),
      // ECMAScript has no lookahead-free negative lookahead, so invalid() is
      // implemented as: youtube host && !valid().
      iregex_(R"(https?://(www\.|m\.|music\.)?(youtube\.com|youtu\.be)\S*)") {}

bool YouTube::valid(const std::string& url) const {
    std::smatch m;
    return std::regex_search(url, m, regex_) && m.position(0) == 0;     // re.match anchors at start
}

bool YouTube::invalid(const std::string& url) const {
    std::smatch m;
    return std::regex_search(url, m, iregex_) && m.position(0) == 0 && !valid(url);
}

static std::string dur_string(long s) {
    char b[32];
    if (s >= 3600) std::snprintf(b, sizeof b, "%ld:%02ld:%02ld", s / 3600, (s % 3600) / 60, s % 60);
    else std::snprintf(b, sizeof b, "%ld:%02ld", s / 60, s % 60);
    return b;
}

Track YouTube::from_json(const void* jp, const std::string& user, bool video) {
    const json& j = *static_cast<const json*>(jp);
    Track t;
    t.id = j.value("id", "");
    t.channel_name = j.value("channel", j.value("uploader", ""));
    long dur = 0;
    if (j.contains("duration") && j["duration"].is_number()) dur = static_cast<long>(j["duration"].get<double>());
    t.duration_sec = static_cast<int>(dur);
    t.duration = dur_string(dur);
    t.title = util::truncate(j.value("title", ""), 25);
    std::string th = j.value("thumbnail", "");
    if (th.empty() && j.contains("thumbnails") && j["thumbnails"].is_array() && !j["thumbnails"].empty())
        th = j["thumbnails"].back().value("url", "");
    t.thumbnail = th.substr(0, th.find('?'));
    // yt-dlp often reports .webp; Telegram only reliably fetches the jpg variant
    if (!t.id.empty()) t.thumbnail = "https://i.ytimg.com/vi/" + t.id + "/hqdefault.jpg";
    std::string url = j.value("webpage_url", "");
    if (url.empty()) url = j.value("url", "");
    if (url.empty() || url.find("youtube.com") == std::string::npos) url = "https://www.youtube.com/watch?v=" + t.id;
    t.url = url.substr(0, url.find("&list="));
    if (j.contains("view_count") && j["view_count"].is_number()) t.view_count = std::to_string(j["view_count"].get<long long>());
    t.user = user;
    t.video = video;
    t.is_youtube = true;
    return t;
}

static std::string tail_of(const std::string& s, size_t n = 400) {
    std::string t = s.size() > n ? s.substr(s.size() - n) : s;
    while (!t.empty() && (t.back() == '\n' || t.back() == '\r')) t.pop_back();
    return t;
}

static long parse_clock(const std::string& s) {      // "1:02:03" / "3:45" -> seconds
    long total = 0, cur = 0;
    for (char ch : s) {
        if (ch >= '0' && ch <= '9') cur = cur * 10 + (ch - '0');
        else if (ch == ':') { total = total * 60 + cur; cur = 0; }
    }
    return total * 60 + cur;
}

static std::string yt_text(const json& v) {         // {"simpleText": ..} or {"runs":[{"text":..}]}
    if (v.is_object()) {
        if (v.contains("simpleText") && v["simpleText"].is_string()) return v["simpleText"].get<std::string>();
        if (v.contains("runs") && v["runs"].is_array()) {
            std::string o;
            for (const auto& r : v["runs"]) o += r.value("text", "");
            return o;
        }
    }
    return "";
}

// Walks an InnerTube search response and returns the video results (live streams are skipped).
std::vector<json> YouTube::parse_search(const std::string& body) {
    std::vector<json> out;
    json root = json::parse(body, nullptr, false);
    if (root.is_discarded()) return out;
    std::function<void(const json&)> walk = [&](const json& j) {
        if (j.is_object()) {
            auto it = j.find("videoRenderer");
            if (it != j.end() && it->is_object()) {
                const json& v = *it;
                std::string id = v.value("videoId", "");
                std::string len = v.contains("lengthText") ? yt_text(v["lengthText"]) : "";
                if (!id.empty() && !len.empty()) {            // no length => live / premiere: skipped
                    json o;
                    o["id"] = id;
                    o["title"] = v.contains("title") ? yt_text(v["title"]) : "";
                    o["channel"] = v.contains("ownerText") ? yt_text(v["ownerText"]) : "";
                    o["duration"] = parse_clock(len);
                    std::string views = v.contains("viewCountText") ? yt_text(v["viewCountText"]) : "";
                    std::string digits;
                    for (char ch : views) if (ch >= '0' && ch <= '9') digits += ch;
                    if (!digits.empty() && digits.size() < 18) o["view_count"] = std::stoll(digits);
                    out.push_back(std::move(o));
                }
                return;
            }
            for (auto& kv : j.items()) walk(kv.value());
        } else if (j.is_array()) {
            for (const auto& x : j) walk(x);
        }
    };
    walk(root);
    return out;
}

static std::string video_id_of(const std::string& url) {
    static const std::regex re(R"((?:youtu\.be/|shorts/|[?&]v=)([A-Za-z0-9_-]{11}))");
    std::smatch m;
    return std::regex_search(url, m, re) ? m[1].str() : "";
}

// Search without yt-dlp: the same InnerTube endpoint the YouTube website uses (no sign-in / cookies needed).
static std::vector<json> innertube_search(const std::string& q) {
    json body = {{"context", {{"client", {{"clientName", "WEB"}, {"clientVersion", "2.20250101.01.00"},
                                         {"hl", "en-IN"}, {"gl", "IN"}}}}},
                 {"query", q}, {"params", "EgIQAQ=="}};
    auto r = net::post_json("https://www.youtube.com/youtubei/v1/search?key=AIzaSyBOti4mM-6x9WDnZIjIeyEU21OpBXqWBgw&prettyPrint=false",
                            body, 20, nullptr,
                            {"Referer: https://www.youtube.com/", "Origin: https://www.youtube.com",
                             "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36"});
    if (r.status != 200) {
        log::warn("YouTube search request failed: HTTP " + std::to_string(r.status) + " " + r.error);
        return {};
    }
    return YouTube::parse_search(r.body);
}

std::optional<Track> YouTube::search(const std::string& q, i64 m_id, bool video) {
    const bool is_url = valid(q);
    const std::string want_id = is_url ? video_id_of(q) : "";
    if (use_innertube) try {
        auto res = innertube_search(want_id.empty() ? q : want_id);
        for (auto& j : res) {
            if (!want_id.empty() && j.value("id", "") != want_id) continue;
            Track t = from_json(&j, "", video);
            if (t.id.empty()) continue;
            t.message_id = m_id;
            log::info("Search hit: " + t.id + " (" + t.duration + ")");
            return t;
        }
        if (res.empty()) log::warn("InnerTube search returned no results for: " + q);
    } catch (const std::exception& e) {
        log::warn(std::string("InnerTube search error: ") + e.what());
    }

    // fallback: yt-dlp
    std::string target = is_url ? q : "ytsearch1:" + q;
    std::vector<std::string> a{yt_dlp, "--dump-json", "--no-warnings", "--no-playlist", "--skip-download",
                               "--playlist-end", "1"};
    std::string cookie = get_cookies();
    if (!cookie.empty()) { a.push_back("--cookies"); a.push_back(cookie); }
    a.push_back("--");
    a.push_back(target);
    auto r = util::exec(a, 60);
    if (r.code != 0 || r.out.empty()) {
        log::warn("yt-dlp search failed (exit " + std::to_string(r.code) + "): " + tail_of(r.err));
        return std::nullopt;
    }
    try {
        std::string first = r.out.substr(0, r.out.find('\n'));
        json j = json::parse(first);
        if (j.value("is_live", false)) return std::nullopt;     // with_live=False
        Track t = from_json(&j, "", video);
        if (t.id.empty()) return std::nullopt;
        t.message_id = m_id;
        return t;
    } catch (...) { return std::nullopt; }
}

std::vector<Track> YouTube::playlist(int limit, const std::string& user, const std::string& url, bool video) {
    std::vector<Track> out;
    auto r = util::exec({yt_dlp, "--flat-playlist", "--dump-json", "--no-warnings", "--playlist-end",
                         std::to_string(limit), "--", url}, 120);
    if (r.code != 0) return out;
    std::istringstream ss(r.out);
    std::string line;
    while (std::getline(ss, line) && static_cast<int>(out.size()) < limit) {
        try {
            json j = json::parse(line);
            Track t = from_json(&j, user, video);
            if (!t.id.empty()) out.push_back(std::move(t));
        } catch (...) {}
    }
    return out;
}

std::string YouTube::get_cookies() {
    std::lock_guard<std::mutex> lk(mu_);
    if (!checked_) {
        std::error_code ec;
        if (fs::exists(cookie_dir_, ec))
            for (const auto& e : fs::directory_iterator(cookie_dir_, ec))
                if (e.path().extension() == ".txt") cookies_.push_back(e.path().string());
        checked_ = true;
    }
    if (cookies_.empty()) {
        if (!warned_) { warned_ = true; log::warn("Cookies are missing; downloads might fail."); }
        return "";
    }
    static std::mt19937 rng{std::random_device{}()};
    return cookies_[std::uniform_int_distribution<size_t>(0, cookies_.size() - 1)(rng)];
}

void YouTube::save_cookies(const std::vector<std::string>& urls) {
    log::info("Saving cookies from urls...");
    fs::create_directories(cookie_dir_);
    for (const auto& u : urls) {
        std::string name = u.substr(u.find_last_of('/') + 1);
        if (name.empty() || name.find_first_not_of("ABCDEFabcdef0123456789abcdefghijklmnopqrstuvwxyzGHIJKLMNOPQRSTUVWXYZ_-") != std::string::npos)
            continue;   // never use attacker-controlled names as a path
        auto r = util::exec({"curl", "-fsSL", "https://batbin.me/raw/" + name}, 60);
        if (r.code == 0) { std::ofstream(cookie_dir_ + "/" + name + ".txt") << r.out; }
        else log::warn("Failed to fetch cookies: " + name);
    }
    std::lock_guard<std::mutex> lk(mu_);
    checked_ = false; cookies_.clear();
    log::info("Cookies saved in " + cookie_dir_ + ".");
}

std::string YouTube::cached(const std::string& id, bool video) {
    for (const char* ext : video ? std::vector<const char*>{"mp4"} : std::vector<const char*>{"mp3", "webm"}) {
        std::string f = "downloads/" + id + "." + ext;
        std::error_code ec;
        if (fs::exists(f, ec) && fs::file_size(f, ec) > 0) return f;
    }
    return "";
}

static std::string url_encode(const std::string& s) {
    static const char* hex = "0123456789ABCDEF";
    std::string o;
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') o += static_cast<char>(c);
        else { o += '%'; o += hex[c >> 4]; o += hex[c & 15]; }
    }
    return o;
}

// Download through the HTTP API (API_URL/download?url=<id>&type=audio|video&api_key=...).
static std::string download_via_api(const std::string& id, bool video) {
    const Config& c = config();
    if (c.API_KEY.empty() || c.API_URL.empty()) return "";
    std::string path = "downloads/" + id + (video ? ".mp4" : ".mp3"), tmp = path + ".part";
    std::string url = c.API_URL + "/download?url=" + url_encode(id) + "&type=" + (video ? "video" : "audio") + "&api_key=" + url_encode(c.API_KEY);
    std::atomic<bool> cancel{false};
    if (!net::download(url, tmp, nullptr, cancel, video ? 600 : 300)) { std::remove(tmp.c_str()); log::warn("API download failed for " + id); return ""; }
    std::error_code ec;
    auto sz = fs::exists(tmp, ec) ? fs::file_size(tmp, ec) : 0;
    bool bad = sz == 0;
    if (!bad && sz < 4096) {                 // tiny "file": probably a JSON/HTML error body, not media
        std::ifstream in(tmp, std::ios::binary);
        char c = 0; in.get(c);
        bad = c == '{' || c == '<' || c == '[';
    }
    if (bad) { log::warn("API download for " + id + " returned no media (check API_URL / API_KEY)"); std::remove(tmp.c_str()); return ""; }
    fs::rename(tmp, path, ec);
    if (!ec) log::info("Downloaded " + id + " via API (" + std::to_string(sz) + " bytes)");
    return ec ? "" : path;
}

std::string YouTube::download(const std::string& id, bool video) {
    // ids come from yt-dlp output, but validate anyway: they become part of a path
    if (id.empty() || id.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-") != std::string::npos)
        return "";
    std::string ext = video ? "mp4" : "webm";
    std::string filename = "downloads/" + id + "." + ext;
    std::string have = cached(id, video);
    if (!have.empty()) return have;
    fs::create_directories("downloads");
    std::string via = download_via_api(id, video);
    if (!via.empty()) return via;      // otherwise fall back to yt-dlp
    log::info(std::string(config().API_KEY.empty() ? "API_KEY not set" : "API download failed") + ": using yt-dlp for " + id);

    std::vector<std::string> a{yt_dlp, "--quiet", "--no-warnings", "--no-playlist", "--geo-bypass",
                               "--no-overwrites", "--no-check-certificates", "-o", "downloads/%(id)s.%(ext)s"};
    std::string cookie = get_cookies();
    if (!cookie.empty()) { a.push_back("--cookies"); a.push_back(cookie); }
    if (video) {
        a.insert(a.end(), {"-f", "(bestvideo[height<=?720][width<=?1280][ext=mp4])+(bestaudio)", "--merge-output-format", "mp4"});
    } else {
        a.insert(a.end(), {"-f", "bestaudio[ext=webm][acodec=opus]"});
    }
    a.push_back("--");
    a.push_back("https://www.youtube.com/watch?v=" + id);
    auto r = util::exec(a, 900);
    if (r.code != 0 || !fs::exists(filename)) {
        log::warn("yt-dlp download failed for " + id + " (exit " + std::to_string(r.code) + "): " + tail_of(r.err));
        return "";
    }
    log::info("Downloaded " + id + " via yt-dlp");
    return filename;
}

YouTube& yt() {
    static YouTube y;
    return y;
}

}  // namespace bot
