// YouTube search, playlist and download. Search uses InnerTube first; playlist and fallback download use the yt-dlp binary
// (spawned without a shell). Needs `yt-dlp` and `ffmpeg` in PATH.
#pragma once
#include <mutex>
#include <optional>
#include <regex>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "../core/types.hpp"

namespace bot {

class YouTube {
public:
    YouTube();
    bool valid(const std::string& url) const;     // supported youtube url
    bool invalid(const std::string& url) const;   // youtube domain but unsupported path
    std::optional<Track> search(const std::string& query_or_url, i64 message_id, bool video = false);
    std::vector<Track> playlist(int limit, const std::string& user, const std::string& url, bool video);
    // Returns the downloaded file path or "" on failure.
    std::string download(const std::string& video_id, bool video = false);
    // Already downloaded file for this id ("" if none): .mp3/.webm for audio, .mp4 for video.
    static std::string cached(const std::string& video_id, bool video);
    // Parses an InnerTube search response into {id,title,channel,duration,view_count} objects (public for tests).
    static std::vector<nlohmann::json> parse_search(const std::string& body);
    void save_cookies(const std::vector<std::string>& urls);
    std::string yt_dlp = "yt-dlp";
    bool use_innertube = true;     // search through YouTube's web API first, yt-dlp as fallback

private:
    std::string get_cookies();
    static Track from_json(const void* j, const std::string& user, bool video);
    std::regex regex_, iregex_;
    std::vector<std::string> cookies_;
    bool checked_ = false, warned_ = false;
    std::mutex mu_;
    std::string cookie_dir_ = "cookies";
};

YouTube& yt();

}  // namespace bot
