// Spotify links -> "title artist" search queries (the audio itself always comes from YouTube).
// Order of attempts: Web API (client credentials, when SPOTIFY_CLIENT_ID/SECRET are set) -> embed page -> oEmbed.
#pragma once
#include <optional>
#include <string>
#include <vector>

namespace bot {

struct SpotifyLink { std::string kind; std::string id; };   // kind: track | album | playlist

struct SpotifyResult {
    std::string name;                    // album / playlist / track title
    std::vector<std::string> queries;    // "Title Artist, Artist"
};

class Spotify {
public:
    static bool is_link(const std::string& url);                           // open.spotify.com/... or spotify:...
    static std::optional<SpotifyLink> parse_link(const std::string& url);
    // pure parsers (unit-tested with fixtures)
    static SpotifyResult parse_embed(const std::string& html, const std::string& kind);
    static SpotifyResult parse_api(const std::string& json_body, const std::string& kind);
    static std::string parse_oembed(const std::string& json_body);

    // network: empty queries on failure
    SpotifyResult resolve(const std::string& url, int limit);

private:
    std::string token();
    std::string fetch(const std::vector<std::string>& argv);
    std::string tok_;
    long long tok_exp_ = 0;
};

Spotify& spotify();

}  // namespace bot
