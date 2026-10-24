// Music-service links (Spotify, Apple Music, JioSaavn, SoundCloud) -> "title artist" search queries.
// The audio itself always comes from YouTube; these services only tell us what the link points to.
#pragma once
#include <optional>
#include <string>
#include <vector>
#include "spotify.hpp"

namespace bot {

struct LinkResult {
    std::string source;                  // "Spotify" | "Apple Music" | "JioSaavn" | "SoundCloud"
    std::string name;
    std::vector<std::string> queries;
};

struct AppleLink { std::string kind; std::string id; std::string country; };   // kind: song | album | playlist
struct SaavnLink { std::string kind; std::string token; };                      // kind: song | album | playlist

namespace links {

// "" when the url is not one of the supported services
std::string source_of(const std::string& url);
inline bool supported(const std::string& url) { return !source_of(url).empty(); }

LinkResult resolve(const std::string& url, int limit);

// ---- pure parsers (unit-tested with fixtures) ----
std::optional<AppleLink> parse_apple_link(const std::string& url);
LinkResult parse_itunes(const std::string& json_body, const std::string& kind);
LinkResult parse_apple_html(const std::string& html);
std::optional<SaavnLink> parse_saavn_link(const std::string& url);
LinkResult parse_saavn(const std::string& json_body, const std::string& kind);
LinkResult parse_ytdlp_lines(const std::string& out);            // yt-dlp --dump-json lines (SoundCloud)
std::string html_decode(std::string s);

}  // namespace links
}  // namespace bot
