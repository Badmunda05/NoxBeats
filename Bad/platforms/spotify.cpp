#include "spotify.hpp"
#include <chrono>
#include <mutex>
#include <nlohmann/json.hpp>
#include "../core/config.hpp"
#include "../core/util.hpp"

namespace bot {
using json = nlohmann::json;

namespace {

std::string join_artists(const json& arr) {
    std::string out;
    if (!arr.is_array()) return out;
    for (const auto& a : arr) {
        std::string n = a.is_object() ? a.value("name", "") : "";
        if (n.empty()) continue;
        if (!out.empty()) out += ", ";
        out += n;
    }
    return out;
}

std::string query_of(const std::string& title, const std::string& artists) {
    if (title.empty()) return "";
    return artists.empty() ? title : title + " " + artists;
}

std::string str(const json& j, const char* k) { return j.is_object() && j.contains(k) && j[k].is_string() ? j[k].get<std::string>() : ""; }

std::string strip_query(std::string s) {
    for (char c : {'?', '#'}) { auto p = s.find(c); if (p != std::string::npos) s.resize(p); }
    return s;
}

}  // namespace

bool Spotify::is_link(const std::string& url) {
    std::string u = util::lower(url);
    return u.rfind("spotify:", 0) == 0 || u.find("open.spotify.com/") != std::string::npos;
}

std::optional<SpotifyLink> Spotify::parse_link(const std::string& url) {
    if (!is_link(url)) return std::nullopt;
    std::string u = strip_query(url);
    std::vector<std::string> parts;
    if (util::lower(u).rfind("spotify:", 0) == 0) {
        size_t p = 0;
        while (true) { size_t q = u.find(':', p); parts.push_back(u.substr(p, q == std::string::npos ? q : q - p)); if (q == std::string::npos) break; p = q + 1; }
        parts.erase(parts.begin());
    } else {
        auto at = util::lower(u).find("open.spotify.com/");
        std::string rest = u.substr(at + 17);
        size_t p = 0;
        while (p <= rest.size()) { size_t q = rest.find('/', p); parts.push_back(rest.substr(p, q == std::string::npos ? q : q - p)); if (q == std::string::npos) break; p = q + 1; }
    }
    // skip "intl-xx" and "embed" prefixes
    while (!parts.empty() && (parts[0].rfind("intl-", 0) == 0 || parts[0] == "embed")) parts.erase(parts.begin());
    // spotify:user:x:playlist:ID
    for (size_t i = 0; i + 1 < parts.size(); ++i) {
        const std::string& k = parts[i];
        if ((k == "track" || k == "album" || k == "playlist") && !parts[i + 1].empty()) {
            std::string id = parts[i + 1];
            for (char ch : id) if (!std::isalnum(static_cast<unsigned char>(ch))) return std::nullopt;
            return SpotifyLink{k, id};
        }
    }
    return std::nullopt;
}

SpotifyResult Spotify::parse_embed(const std::string& html, const std::string& kind) {
    SpotifyResult r;
    const std::string tag = "id=\"__NEXT_DATA__\"";
    auto p = html.find(tag);
    if (p == std::string::npos) return r;
    auto s = html.find('>', p);
    auto e = html.find("</script>", s);
    if (s == std::string::npos || e == std::string::npos) return r;
    json j = json::parse(html.substr(s + 1, e - s - 1), nullptr, false);
    if (j.is_discarded()) return r;
    const json* ent = &j;
    for (const char* k : {"props", "pageProps", "state", "data", "entity"}) {
        if (!ent->is_object() || !ent->contains(k)) return r;
        ent = &(*ent)[k];
    }
    r.name = str(*ent, "name");
    if (r.name.empty()) r.name = str(*ent, "title");
    if (kind == "track") {
        std::string artists = join_artists((*ent)["artists"]);
        if (artists.empty()) artists = str(*ent, "subtitle");
        std::string q = query_of(r.name, artists);
        if (!q.empty()) r.queries.push_back(q);
    } else if (ent->contains("trackList") && (*ent)["trackList"].is_array()) {
        for (const auto& t : (*ent)["trackList"]) {
            std::string q = query_of(str(t, "title"), str(t, "subtitle"));
            if (!q.empty()) r.queries.push_back(q);
        }
    }
    return r;
}

SpotifyResult Spotify::parse_api(const std::string& body, const std::string& kind) {
    SpotifyResult r;
    json j = json::parse(body, nullptr, false);
    if (j.is_discarded() || !j.is_object()) return r;
    r.name = str(j, "name");
    auto add = [&](const json& t) {
        std::string q = query_of(str(t, "name"), join_artists(t.contains("artists") ? t["artists"] : json()));
        if (!q.empty()) r.queries.push_back(q);
    };
    if (kind == "track") add(j);
    else if (kind == "album") {
        if (j.contains("tracks") && j["tracks"].contains("items"))
            for (const auto& t : j["tracks"]["items"]) add(t);
    } else if (j.contains("tracks") && j["tracks"].is_object() && j["tracks"].contains("items")) {
        for (const auto& it : j["tracks"]["items"])
            if (it.is_object() && it.contains("track") && it["track"].is_object()) add(it["track"]);
    }
    return r;
}

std::string Spotify::parse_oembed(const std::string& body) {
    json j = json::parse(body, nullptr, false);
    return j.is_discarded() ? "" : str(j, "title");
}

std::string Spotify::fetch(const std::vector<std::string>& argv) {
    auto r = util::exec(argv, 25);
    return r.code == 0 ? r.out : "";
}

std::string Spotify::token() {
    const auto& cfg = config();
    if (cfg.SPOTIFY_CLIENT_ID.empty() || cfg.SPOTIFY_CLIENT_SECRET.empty()) return "";
    long long now = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    if (!tok_.empty() && now < tok_exp_ - 30) return tok_;
    std::string out = fetch({"curl", "-s", "-m", "20", "-u", cfg.SPOTIFY_CLIENT_ID + ":" + cfg.SPOTIFY_CLIENT_SECRET,
                             "-d", "grant_type=client_credentials", "https://accounts.spotify.com/api/token"});
    json j = json::parse(out, nullptr, false);
    if (j.is_discarded() || !j.contains("access_token")) return "";
    tok_ = j["access_token"].get<std::string>();
    tok_exp_ = now + j.value("expires_in", 3600);
    return tok_;
}

SpotifyResult Spotify::resolve(const std::string& url, int limit) {
    static std::mutex mu;
    std::lock_guard<std::mutex> lk(mu);
    SpotifyResult res;
    auto link = parse_link(url);
    if (!link) return res;

    std::string tk = token();
    if (!tk.empty()) {
        std::string api = "https://api.spotify.com/v1/" + link->kind + "s/" + link->id;
        if (link->kind == "playlist") api += "?fields=name,tracks.items(track(name,artists(name)))&limit=" + std::to_string(limit);
        res = parse_api(fetch({"curl", "-s", "-m", "20", "-H", "Authorization: Bearer " + tk, api}), link->kind);
    }
    if (res.queries.empty()) {
        std::string html = fetch({"curl", "-sL", "-m", "20", "-A", "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124 Safari/537.36",
                                  "https://open.spotify.com/embed/" + link->kind + "/" + link->id});
        res = parse_embed(html, link->kind);
    }
    if (res.queries.empty() && link->kind == "track") {
        std::string t = parse_oembed(fetch({"curl", "-sL", "-m", "15", "https://open.spotify.com/oembed?url=https://open.spotify.com/track/" + link->id}));
        if (!t.empty()) { res.name = t; res.queries.push_back(t); }
    }
    if (static_cast<int>(res.queries.size()) > limit) res.queries.resize(limit);
    return res;
}

Spotify& spotify() { static Spotify s; return s; }

}  // namespace bot
