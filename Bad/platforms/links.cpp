#include "links.hpp"
#include <algorithm>
#include <regex>
#include <sstream>
#include <nlohmann/json.hpp>
#include "yt.hpp"
#include "../core/util.hpp"

namespace bot::links {
using json = nlohmann::json;

namespace {

const char* kUA = "Mozilla/5.0 (X11; Linux x86_64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124 Safari/537.36";

std::string fetch(const std::vector<std::string>& argv, int timeout = 25) {
    auto r = util::exec(argv, timeout);
    return r.code == 0 ? r.out : "";
}
std::string get_page(const std::string& url) { return fetch({"curl", "-sL", "-m", "20", "-A", kUA, url}); }

std::string s_of(const json& j, const char* k) { return j.is_object() && j.contains(k) && j[k].is_string() ? j[k].get<std::string>() : ""; }
std::string q_of(const std::string& title, const std::string& artist) {
    if (title.empty()) return "";
    return artist.empty() ? title : title + " " + artist;
}
std::string strip_query(std::string s) {
    for (char c : {'?', '#'}) { auto p = s.find(c); if (p != std::string::npos) s.resize(p); }
    return s;
}
std::vector<std::string> split(const std::string& s, char d) {
    std::vector<std::string> out; std::string cur;
    for (char c : s) { if (c == d) { out.push_back(cur); cur.clear(); } else cur += c; }
    out.push_back(cur);
    return out;
}
std::string meta(const std::string& html, const std::string& prop) {
    auto p = html.find("property=\"" + prop + "\"");
    if (p == std::string::npos) return "";
    auto tag_start = html.rfind('<', p), tag_end = html.find('>', p);
    if (tag_start == std::string::npos || tag_end == std::string::npos) return "";
    std::string tag = html.substr(tag_start, tag_end - tag_start);
    auto c = tag.find("content=\"");
    if (c == std::string::npos) return "";
    c += 9;
    auto e = tag.find('"', c);
    return e == std::string::npos ? "" : html_decode(tag.substr(c, e - c));
}

}  // namespace

std::string html_decode(std::string s) {
    static const std::vector<std::pair<std::string, std::string>> m = {
        {"&quot;", "\""}, {"&#039;", "'"}, {"&#39;", "'"}, {"&apos;", "'"}, {"&lt;", "<"}, {"&gt;", ">"}, {"&amp;", "&"}};
    for (const auto& [a, b] : m) {
        size_t p = 0;
        while ((p = s.find(a, p)) != std::string::npos) { s.replace(p, a.size(), b); p += b.size(); }
    }
    return s;
}

// ------------------------------------------------------------------ detection
std::string source_of(const std::string& url) {
    std::string u = util::lower(url);
    if (Spotify::is_link(url)) return "Spotify";
    if (u.find("music.apple.com/") != std::string::npos) return "Apple Music";
    if (u.find("jiosaavn.com/") != std::string::npos || u.find("saavn.com/") != std::string::npos) return "JioSaavn";
    if (u.find("soundcloud.com/") != std::string::npos || u.find("on.soundcloud.com/") != std::string::npos) return "SoundCloud";
    return "";
}

// ------------------------------------------------------------------ Apple Music
std::optional<AppleLink> parse_apple_link(const std::string& url) {
    std::string u = strip_query(url);
    std::string q = url.size() > u.size() ? url.substr(u.size()) : "";
    auto at = util::lower(u).find("music.apple.com/");
    if (at == std::string::npos) return std::nullopt;
    auto parts = split(u.substr(at + 16), '/');
    if (parts.empty()) return std::nullopt;
    AppleLink l;
    size_t i = 0;
    if (parts[0].size() == 2) { l.country = parts[0]; i = 1; } else l.country = "us";
    if (i >= parts.size()) return std::nullopt;
    l.kind = parts[i];
    if (l.kind != "song" && l.kind != "album" && l.kind != "playlist") return std::nullopt;
    l.id = parts.back();
    if (l.id.empty() && parts.size() >= 2) l.id = parts[parts.size() - 2];
    if (l.kind == "album") {                                   // album link with ?i=<song id> is that one song
        auto p = q.find("i=");
        if (p != std::string::npos) {
            std::string sid;
            for (size_t k = p + 2; k < q.size() && std::isdigit(static_cast<unsigned char>(q[k])); ++k) sid += q[k];
            if (!sid.empty()) { l.kind = "song"; l.id = sid; }
        }
    }
    if (l.id.empty()) return std::nullopt;
    for (char c : l.id) if (!std::isalnum(static_cast<unsigned char>(c)) && c != '.' && c != '-' && c != '_') return std::nullopt;
    return l;
}

LinkResult parse_itunes(const std::string& body, const std::string& kind) {
    LinkResult r; r.source = "Apple Music";
    json j = json::parse(body, nullptr, false);
    if (j.is_discarded() || !j.contains("results") || !j["results"].is_array()) return r;
    for (const auto& it : j["results"]) {
        std::string wt = s_of(it, "wrapperType"), k = s_of(it, "kind");
        if (wt == "collection" || wt == "artist") { if (r.name.empty()) r.name = s_of(it, "collectionName"); continue; }
        if (wt == "track" || k == "song") {
            std::string q = q_of(s_of(it, "trackName"), s_of(it, "artistName"));
            if (!q.empty()) r.queries.push_back(q);
        }
    }
    if (kind == "song" && r.queries.size() > 1) r.queries.resize(1);
    return r;
}

LinkResult parse_apple_html(const std::string& html) {
    LinkResult r; r.source = "Apple Music";
    // JSON-LD (<script type="application/ld+json">) carries the track list of albums and playlists
    size_t p = 0;
    while ((p = html.find("application/ld+json", p)) != std::string::npos) {
        auto s = html.find('>', p), e = html.find("</script>", p);
        if (s == std::string::npos || e == std::string::npos) break;
        json j = json::parse(html.substr(s + 1, e - s - 1), nullptr, false);
        p = e;
        if (j.is_discarded() || !j.is_object() || !j.contains("track") || !j["track"].is_array()) continue;
        r.name = s_of(j, "name");
        std::string by = j.contains("byArtist") ? (j["byArtist"].is_array() && !j["byArtist"].empty() ? s_of(j["byArtist"][0], "name") : s_of(j["byArtist"], "name")) : "";
        for (const auto& t : j["track"]) {
            std::string a = t.contains("byArtist") ? (t["byArtist"].is_array() && !t["byArtist"].empty() ? s_of(t["byArtist"][0], "name") : s_of(t["byArtist"], "name")) : by;
            std::string q = q_of(s_of(t, "name"), a);
            if (!q.empty()) r.queries.push_back(q);
        }
        if (!r.queries.empty()) return r;
    }
    // fallback: <meta property="og:title" content="Song by Artist on Apple Music">
    std::string t = meta(html, "og:title");
    if (t.empty()) return r;
    for (const char* suf : {" on Apple Music", " | Apple Music"}) {
        auto pos = t.rfind(suf);
        if (pos != std::string::npos && pos + std::string(suf).size() == t.size()) t.resize(pos);
    }
    for (const char* suf : {" - Single", " - EP"}) {
        auto pos = t.find(suf);
        if (pos != std::string::npos) t.erase(pos, std::string(suf).size());
    }
    auto by = t.rfind(" by ");
    r.name = t;
    r.queries.push_back(by == std::string::npos ? t : t.substr(0, by) + " " + t.substr(by + 4));
    return r;
}

// ------------------------------------------------------------------ JioSaavn
std::optional<SaavnLink> parse_saavn_link(const std::string& url) {
    std::string u = strip_query(url);
    auto at = util::lower(u).find("saavn.com/");
    if (at == std::string::npos) return std::nullopt;
    auto parts = split(u.substr(at + 10), '/');
    while (!parts.empty() && parts.back().empty()) parts.pop_back();
    if (parts.size() < 2) return std::nullopt;
    SaavnLink l;
    std::string k = parts[0] == "s" && parts.size() > 1 ? parts[1] : parts[0];
    if (k == "song") l.kind = "song";
    else if (k == "album") l.kind = "album";
    else if (k == "playlist" || k == "featured" || k == "s") l.kind = "playlist";
    else return std::nullopt;
    l.token = parts.back();
    for (char c : l.token) if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_' && c != '-' && c != ',') return std::nullopt;
    return l.token.empty() ? std::nullopt : std::optional<SaavnLink>(l);
}

static std::string saavn_artist(const json& it) {
    std::string out;
    auto add = [&](const std::string& n) { if (!n.empty() && out.find(n) == std::string::npos) out += (out.empty() ? "" : ", ") + n; };
    if (it.contains("more_info") && it["more_info"].is_object()) {
        const json& mi = it["more_info"];
        if (mi.contains("artistMap") && mi["artistMap"].contains("primary_artists") && mi["artistMap"]["primary_artists"].is_array())
            for (const auto& a : mi["artistMap"]["primary_artists"]) add(html_decode(s_of(a, "name")));
        if (out.empty()) add(html_decode(s_of(mi, "music")));
    }
    if (out.empty()) add(html_decode(s_of(it, "primary_artists")));
    if (out.empty()) add(html_decode(s_of(it, "subtitle")));
    return out;
}

LinkResult parse_saavn(const std::string& body, const std::string& kind) {
    LinkResult r; r.source = "JioSaavn";
    json j = json::parse(body, nullptr, false);
    if (j.is_discarded()) return r;
    auto add = [&](const json& it) {
        std::string title = html_decode(s_of(it, "title"));
        if (title.empty()) title = html_decode(s_of(it, "song"));
        std::string q = q_of(title, saavn_artist(it));
        if (!q.empty()) r.queries.push_back(q);
    };
    const json* root = &j;
    if (j.is_object() && j.contains("data") && j["data"].is_object()) root = &j["data"];
    if (root->is_object()) r.name = html_decode(s_of(*root, "title"));
    if (root->is_object() && root->contains("songs") && (*root)["songs"].is_array()) for (const auto& it : (*root)["songs"]) add(it);
    else if (root->is_object() && root->contains("list") && (*root)["list"].is_array()) for (const auto& it : (*root)["list"]) add(it);
    else if (root->is_array()) for (const auto& it : *root) add(it);
    else if (root->is_object() && kind == "song") add(*root);
    return r;
}

// ------------------------------------------------------------------ SoundCloud (through yt-dlp metadata)
LinkResult parse_ytdlp_lines(const std::string& out) {
    LinkResult r; r.source = "SoundCloud";
    std::istringstream ss(out);
    std::string line;
    while (std::getline(ss, line)) {
        json j = json::parse(line, nullptr, false);
        if (j.is_discarded() || !j.is_object()) continue;
        std::string title = s_of(j, "title");
        std::string who = s_of(j, "uploader");
        if (who.empty()) who = s_of(j, "artist");
        if (r.name.empty()) r.name = s_of(j, "playlist_title").empty() ? title : s_of(j, "playlist_title");
        std::string q = q_of(title, who);
        if (!q.empty()) r.queries.push_back(q);
    }
    return r;
}

// ------------------------------------------------------------------ network
LinkResult resolve(const std::string& url, int limit) {
    LinkResult res;
    res.source = source_of(url);
    if (res.source == "Spotify") {
        auto sp = spotify().resolve(url, limit);
        res.name = sp.name; res.queries = sp.queries;
    } else if (res.source == "Apple Music") {
        auto l = parse_apple_link(url);
        if (l) {
            if (l->kind == "song") res = parse_itunes(fetch({"curl", "-s", "-m", "20", "https://itunes.apple.com/lookup?id=" + l->id + "&country=" + l->country}), "song");
            else if (l->kind == "album") res = parse_itunes(fetch({"curl", "-s", "-m", "20", "https://itunes.apple.com/lookup?id=" + l->id + "&entity=song&country=" + l->country}), "album");
            if (res.queries.empty()) res = parse_apple_html(get_page(strip_query(url)));
        }
        res.source = "Apple Music";
    } else if (res.source == "JioSaavn") {
        auto l = parse_saavn_link(url);
        if (l) {
            std::string api = "https://www.jiosaavn.com/api.php?__call=webapi.get&token=" + l->token + "&type=" + l->kind +
                              "&n=" + std::to_string(limit) + "&p=1&_format=json&_marker=0&ctx=web6dot0";
            res = parse_saavn(fetch({"curl", "-s", "-m", "20", "-A", kUA, api}), l->kind);
        }
        if (res.queries.empty()) {   // page title: "Song Name Song | Artist ..." -> best effort
            std::string t = meta(get_page(strip_query(url)), "og:title");
            auto bar = t.find(" - ");
            if (!t.empty()) res.queries.push_back(bar == std::string::npos ? t : t.substr(0, bar));
        }
        res.source = "JioSaavn";
    } else if (res.source == "SoundCloud") {
        auto r = util::exec({yt().yt_dlp, "--dump-json", "--flat-playlist", "--no-warnings", "--playlist-end", std::to_string(limit), "--", strip_query(url)}, 90);
        if (r.code == 0) res = parse_ytdlp_lines(r.out);
        res.source = "SoundCloud";
    }
    if (static_cast<int>(res.queries.size()) > limit) res.queries.resize(limit);
    return res;
}

}  // namespace bot::links
