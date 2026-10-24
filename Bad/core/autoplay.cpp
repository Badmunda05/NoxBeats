#include "autoplay.hpp"
#include <algorithm>
#include <chrono>
#include <functional>
#include <random>
#include <thread>
#include "config.hpp"
#include "log.hpp"
#include "queue.hpp"
#include "util.hpp"
#include "../db/db.hpp"
#include "../platforms/yt.hpp"

namespace bot {
namespace {

constexpr size_t kBufferSize = 6;          // ready-to-play suggestions kept per chat
constexpr size_t kMinBuffer = 2;           // refill in the background when fewer than this are left
constexpr size_t kHistorySize = 80;        // remembered songs per chat (no repeats)
constexpr int kFetchTimeoutSec = 25;       // longest wait for suggestions when the queue is empty

std::mt19937& rng() { static std::mt19937 r{std::random_device{}()}; return r; }

bool alnum(char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9'); }

// whole-word / whole-phrase match on already lower-cased text
bool has_phrase(const std::string& t, const std::string& p) {
    for (size_t pos = t.find(p); pos != std::string::npos; pos = t.find(p, pos + 1)) {
        bool left = pos == 0 || !alnum(t[pos - 1]);
        bool right = pos + p.size() >= t.size() || !alnum(t[pos + p.size()]);
        if (left && right) return true;
    }
    return false;
}

bool has_any(const std::string& t, const std::vector<std::string>& words) {
    for (const auto& w : words) if (t.find(w) != std::string::npos) return true;
    return false;
}

bool valid_id(const std::string& s) {
    if (s.size() != 11) return false;
    for (char c : s) if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-')) return false;
    return true;
}

const std::map<std::string, std::vector<std::string>>& mood_keywords() {
    static const std::map<std::string, std::vector<std::string>> m = {
        {"romantic", {"love", "pyaar", "ishq", "mohabbat", "romantic", "dil", "sanam", "tera", "meri", "heart"}},
        {"sad", {"sad", "dard", "judaa", "tanha", "alone", "broken", "tears", "heartbroken", "cry"}},
        {"happy", {"party", "dance", "nacho", "bhangra", "celebration", "happy", "item", "fun"}},
        {"party", {"party", "club", "dj", "remix", "dance", "bass", "rave"}},
        {"chill", {"chill", "relax", "lofi", "acoustic", "unplugged", "soft", "slow", "ambient"}},
        {"workout", {"workout", "gym", "motivation", "energy", "power", "beast", "pump", "fitness"}},
        {"bhajan", {"bhajan", "aarti", "stotram", "mantra", "shiv", "krishna", "ram", "hanuman", "devotional", "god"}},
        {"retro", {"old", "classic", "vintage", "70s", "80s", "90s", "retro"}},
        {"rap", {"rap", "hip hop", "freestyle", "diss", "flow", "bars"}},
        {"acoustic", {"acoustic", "unplugged", "live acoustic", "guitar", "piano"}},
        {"funk", {"funk", "carioca", "baile", "mc ", "beat"}},
        {"phonk", {"phonk", "drift", "cowbell", "brazilian phonk", "russian phonk"}},
    };
    return m;
}

const std::map<std::string, std::vector<std::string>>& lang_keywords() {
    static const std::map<std::string, std::vector<std::string>> m = {
        {"hindi", {"hindi", "bollywood"}}, {"punjabi", {"punjabi", "jatt", "munde", "kudi", "bhangra"}},
        {"english", {"english", "pop", "rock", "hip hop"}}, {"tamil", {"tamil", "kollywood"}},
        {"telugu", {"telugu", "tollywood"}}, {"kannada", {"kannada", "sandalwood"}},
        {"malayalam", {"malayalam", "mollywood"}}, {"bengali", {"bengali"}}, {"marathi", {"marathi"}},
        {"gujarati", {"gujarati"}}, {"bhojpuri", {"bhojpuri"}}, {"haryanvi", {"haryanvi"}},
        {"urdu", {"urdu", "pakistani"}}, {"arabic", {"arabic"}}, {"turkish", {"turkish"}},
        {"korean", {"korean", "kpop"}}, {"japanese", {"japanese", "jpop", "anime"}},
        {"spanish", {"spanish", "latin"}}, {"french", {"french"}},
        {"brazilian", {"brazilian", "portuguese", "funk carioca", "sertanejo"}},
        {"russian", {"russian"}}, {"chinese", {"chinese", "mandopop"}},
    };
    return m;
}

std::vector<Autoplay::Cand> from_json_list(const std::vector<nlohmann::json>& in) {
    std::vector<Autoplay::Cand> out;
    for (const auto& j : in) {
        Autoplay::Cand c;
        c.id = j.value("id", "");
        c.title = j.value("title", "");
        c.channel = util::lower(j.value("channel", ""));
        if (j.contains("duration") && j["duration"].is_number()) c.sec = static_cast<int>(j["duration"].get<double>());
        if (!c.id.empty() && !c.title.empty()) out.push_back(std::move(c));
    }
    return out;
}

std::vector<Autoplay::Cand> soft_shuffle(std::vector<Autoplay::Cand> v, size_t group = 3) {
    // keep the "most related first" order but avoid the same chain every time
    for (size_t i = 0; i < v.size(); i += group)
        std::shuffle(v.begin() + i, v.begin() + std::min(v.size(), i + group), rng());
    return v;
}

}  // namespace

// ---------------------------------------------------------------- helpers
const std::vector<std::string>& Autoplay::langs() {
    static const std::vector<std::string> v = [] {
        std::vector<std::string> r{"auto"};
        for (const auto& [k, _] : autoplay_channels()) r.push_back(k);
        return r;
    }();
    return v;
}

const std::vector<std::string>& Autoplay::moods() {
    static const std::vector<std::string> v = [] {
        std::vector<std::string> r{"any"};
        auto it = autoplay_channels().find("hindi");
        if (it != autoplay_channels().end()) for (const auto& [k, _] : it->second) r.push_back(k);
        return r;
    }();
    return v;
}

std::string Autoplay::extract_song_name(const std::string& title) {
    std::string t = util::lower(title);
    auto strip = [&](char open, char close) {
        for (;;) {
            size_t a = t.find(open);
            if (a == std::string::npos) return;
            size_t b = t.find(close, a + 1);
            if (b == std::string::npos) return;
            t.erase(a, b - a + 1);
        }
    };
    strip('(', ')');
    strip('[', ']');
    for (const char* sep : {" | ", " - ", " by ", " from ", " ft. ", " feat. "}) {
        size_t p = t.find(sep);
        if (p != std::string::npos) t.resize(p);
    }
    static const std::set<std::string> noise = {"official", "video", "audio", "full", "song", "title", "hd", "4k", "8k", "new", "latest", "lyrics", "lyrical"};
    std::string joined;
    for (const auto& w : util::split_ws(t)) {
        if (noise.count(w)) continue;
        joined += (joined.empty() ? "" : " ") + w;
    }
    std::string clean;
    for (unsigned char c : joined) if (std::isalnum(c) || std::isspace(c) || c >= 0x80) clean += static_cast<char>(c);   // keep non-ASCII letters
    std::string out;
    for (const auto& w : util::split_ws(clean)) out += (out.empty() ? "" : " ") + w;
    return out;
}

bool Autoplay::is_same_song(const std::string& a, const std::string& b) {
    std::string s1 = extract_song_name(a), s2 = extract_song_name(b);
    if (s1.empty() || s2.empty()) return false;
    if (s1 == s2) return true;
    auto w1 = util::split_ws(s1), w2 = util::split_ws(s2);
    std::set<std::string> a1(w1.begin(), w1.end()), a2(w2.begin(), w2.end());
    if (a1.empty() || a2.empty()) return false;
    size_t common = 0;
    for (const auto& w : a1) if (a2.count(w)) ++common;
    return static_cast<double>(common) / static_cast<double>(std::min(a1.size(), a2.size())) >= 0.7;
}

bool Autoplay::is_bad_song(const std::string& title, int sec) {
    if (title.empty()) return true;
    std::string t = util::lower(title);
    if (t.rfind("http", 0) == 0 || t.find("youtu.be/") != std::string::npos || t.find("youtube.com/") != std::string::npos) return true;
    static const std::vector<std::string> bad = {
        // playlists / compilations
        "jukebox", "mashup", "compilation", "playlist", "medley", "non stop", "non-stop", "nonstop", "megamix", "full album",
        "top 10", "top 20", "top 50", "best of", "hits of", "songs of",
        // movies / promos
        "full movie", "movie clip", "movie scene", "official trailer", "trailer", "teaser", "promo", "making of",
        "behind the scenes", "bts video",
        // live streams / loops
        "live stream", "livestream", "watch live", "streaming now", "24/7", "24x7", "hour loop", "1 hour", "10 hours", "hours of",
        // talks / reactions
        "podcast", "interview", "press conference", "reaction", "reacts to", "review", "explained",
        // shorts / gaming / news / random
        "#shorts", "youtube shorts", "short video", "gameplay", "walkthrough", "gaming", "breaking news", "news update",
        "food vlog", "travel vlog", "recipe", "cooking",
        "mix",   // whole word only, so "remix" versions are still allowed
    };
    for (const auto& p : bad) if (has_phrase(t, p)) return true;
    return sec < 90 || sec > 900;
}

std::vector<Autoplay::Cand> Autoplay::filter_by_mood(const std::vector<Cand>& v, const std::string& mood) {
    if (mood.empty() || mood == "any") return v;
    auto it = mood_keywords().find(mood);
    if (it == mood_keywords().end()) return v;
    std::vector<Cand> out;
    for (const auto& c : v) if (has_any(util::lower(c.title), it->second)) out.push_back(c);
    return out.empty() ? v : out;
}

std::vector<Autoplay::Cand> Autoplay::filter_by_language(const std::vector<Cand>& v, const std::string& lang) {
    if (lang.empty() || lang == "auto") return v;
    auto it = lang_keywords().find(lang);
    if (it == lang_keywords().end()) return v;
    static const std::vector<std::string> other_indian = {"punjabi", "tamil", "telugu", "kannada", "malayalam", "bengali", "marathi", "gujarati", "bhojpuri", "haryanvi"};
    std::vector<Cand> out;
    for (const auto& c : v) {
        std::string t = util::lower(c.title);
        if (lang == "hindi") {
            if (!has_any(t, other_indian) && !has_any(c.channel, other_indian)) out.push_back(c);
        } else if (has_any(t, it->second) || has_any(c.channel, it->second)) {
            out.push_back(c);
        }
    }
    return out.empty() ? v : out;
}

// ---------------------------------------------------------------- state
Autoplay::~Autoplay() {
    std::unique_lock<std::mutex> lk(mu_);
    cv_.wait_for(lk, std::chrono::seconds(8), [&] { return active_ == 0; });   // let background searches finish
}

void Autoplay::remember_locked(i64 chat, const std::string& id, const std::string& title) {
    auto& h = history_[chat];
    if (!h.empty() && h.back().first == id && h.back().second == title) return;
    h.emplace_back(id, title);
    while (h.size() > kHistorySize) h.pop_front();
}

// already played, already queued, or just another upload of the same song
bool Autoplay::is_dup(i64 chat, const std::string& id, const std::string& title) {
    std::vector<std::string> known;
    {
        std::lock_guard<std::mutex> lk(mu_);
        auto& h = history_[chat];
        for (const auto& e : h) if (e.first == id) return true;
        size_t from = h.size() > 15 ? h.size() - 15 : 0;
        for (size_t i = from; i < h.size(); ++i) known.push_back(h[i].second);
    }
    for (const auto& t : queue().get_queue(chat)) {
        if (t->id == id) return true;
        known.push_back(t->title);
    }
    for (const auto& k : known) if (!k.empty() && is_same_song(k, title)) return true;
    return false;
}

std::vector<Autoplay::Cand> Autoplay::usable(const std::vector<Cand>& in, i64 chat, const std::string& seed_vid) {
    std::set<std::string> seen;
    std::vector<Cand> out;
    for (const auto& c : in) {
        if (c.id == seed_vid || !seen.insert(c.id).second) continue;
        if (is_bad_song(c.title, c.sec)) continue;
        if (c.sec > config().DURATION_LIMIT) continue;
        if (is_dup(chat, c.id, c.title)) continue;
        out.push_back(c);
    }
    return out;
}

std::vector<Autoplay::Cand> Autoplay::channel_candidates(const std::string& lang, const std::string& mood) {
    auto lit = autoplay_channels().find(lang);
    if (lit == autoplay_channels().end()) return {};
    const std::vector<std::string>* names = nullptr;
    if (mood != "any" && !mood.empty()) {
        auto mit = lit->second.find(mood);
        if (mit != lit->second.end() && !mit->second.empty()) names = &mit->second;
    }
    if (!names) {
        std::vector<const std::vector<std::string>*> all;
        for (const auto& [_, v] : lit->second) if (!v.empty()) all.push_back(&v);
        if (all.empty()) return {};
        names = all[std::uniform_int_distribution<size_t>(0, all.size() - 1)(rng())];
    }
    std::vector<std::string> picks = *names;
    std::shuffle(picks.begin(), picks.end(), rng());
    if (picks.size() > 3) picks.resize(3);
    static const std::set<std::string> big = {"t-series", "sony music", "zee music", "shemaroo", "rotana", "mazzika", "netd"};
    std::vector<Cand> out;
    for (const auto& name : picks) {
        std::string lname = util::lower(name);
        std::string first = util::split_ws(lname).empty() ? lname : util::split_ws(lname)[0];
        for (auto& c : search_fn(name)) {
            if (!big.count(lname) && c.channel.find(first) == std::string::npos) continue;   // only the artist's own channel
            out.push_back(std::move(c));
        }
    }
    return out;
}

// Best next songs for `seed`: YouTube's own suggestions first, search as backup.
std::vector<Autoplay::Cand> Autoplay::fetch_candidates(i64 chat, const TrackPtr& seed) {
    std::string lang = db().get_ap_lang(chat), mood = db().get_ap_mood(chat);
    bool prefs = (lang != "" && lang != "auto") || (mood != "" && mood != "any");
    std::string seed_vid = seed && seed->is_youtube && valid_id(seed->id) ? seed->id : "";
    auto apply = [&](const std::vector<Cand>& v) { return prefs ? filter_by_language(filter_by_mood(v, mood), lang) : v; };

    std::vector<Cand> pool;
    if (!seed_vid.empty()) pool = related_fn(seed_vid);
    auto good = usable(apply(pool), chat, seed_vid);

    if (good.size() < kBufferSize) {
        std::vector<std::string> queries;
        if (seed && seed->is_youtube) {
            std::string name = extract_song_name(seed->title);
            if (!name.empty()) { queries.push_back(name + " similar songs"); queries.push_back("songs like " + name); }
        }
        std::string l = lang == "auto" ? "" : lang, m = mood == "any" ? "" : mood;
        if (!l.empty() || !m.empty()) queries.push_back(util::trim(l + " " + m + " songs"));
        if (queries.empty()) queries.push_back("latest songs");
        std::vector<Cand> extra;
        for (const auto& q : queries) for (auto& c : search_fn(q)) extra.push_back(std::move(c));
        if (prefs && !l.empty()) for (auto& c : channel_candidates(l, mood)) extra.push_back(std::move(c));
        std::vector<Cand> all = pool;
        all.insert(all.end(), extra.begin(), extra.end());
        good = usable(apply(all), chat, seed_vid);
    }
    if (good.empty()) {
        // last resort so the music never stops: popular songs (in the chosen language)
        std::string base = (lang == "auto" || lang.empty()) ? "" : lang + " ";
        std::vector<Cand> all = pool;
        for (auto& c : search_fn(base + "trending songs")) all.push_back(std::move(c));
        for (auto& c : search_fn(base + "latest hit songs")) all.push_back(std::move(c));
        good = usable(all, chat, seed_vid);
    }
    return soft_shuffle(std::move(good));
}

void Autoplay::fill(i64 chat, const TrackPtr& seed) {
    std::vector<Cand> cands;
    try { cands = fetch_candidates(chat, seed); }
    catch (const std::exception& e) { log::warn(std::string("Autoplay fetch failed: ") + e.what()); return; }
    if (!db().get_autoplay(chat)) return;   // switched off while we were searching
    std::lock_guard<std::mutex> lk(mu_);
    auto& buf = buffer_[chat];
    for (auto& c : cands) {
        if (buf.size() >= kBufferSize) break;
        bool dup = false;
        for (const auto& b : buf) if (b.id == c.id || is_same_song(b.title, c.title)) { dup = true; break; }
        if (!dup) buf.push_back(std::move(c));
    }
}

void Autoplay::start_fill(i64 chat, const TrackPtr& seed) {
    {
        std::lock_guard<std::mutex> lk(mu_);
        if (!filling_.insert(chat).second) return;
        ++active_;
    }
    std::thread([this, chat, seed] {
        try { fill(chat, seed); } catch (...) {}
        std::lock_guard<std::mutex> lk(mu_);
        filling_.erase(chat);
        --active_;
        cv_.notify_all();
    }).detach();
}

bool Autoplay::pop_valid(i64 chat, Cand& out) {
    for (;;) {
        Cand c;
        {
            std::lock_guard<std::mutex> lk(mu_);
            auto& b = buffer_[chat];
            if (b.empty()) return false;
            c = b.front();
            b.erase(b.begin());
        }
        if (!is_dup(chat, c.id, c.title)) { out = std::move(c); return true; }
    }
}

void Autoplay::on_track_start(i64 chat, const TrackPtr& track) {
    try {
        if (!track || !db().get_autoplay(chat)) return;
        bool own = track->user == "Autoplay";
        size_t have;
        {
            std::lock_guard<std::mutex> lk(mu_);
            remember_locked(chat, track->id, track->title);
            if (!own) buffer_.erase(chat);   // a person picked this song: forget suggestions made for older ones
            have = buffer_[chat].size();
        }
        if (have >= kMinBuffer) return;
        start_fill(chat, track);
    } catch (const std::exception& e) {
        log::warn(std::string("Autoplay prefetch error: ") + e.what());
    }
}

void Autoplay::reset(i64 chat) {
    std::lock_guard<std::mutex> lk(mu_);
    buffer_.erase(chat);
}

TrackPtr Autoplay::next(i64 chat, const TrackPtr& last) {
    try {
        if (!last || !db().get_autoplay(chat)) return nullptr;
        {
            std::lock_guard<std::mutex> lk(mu_);
            remember_locked(chat, last->id, last->title);
        }
        Cand c;
        bool ok = pop_valid(chat, c);
        if (!ok) {
            start_fill(chat, last);
            std::unique_lock<std::mutex> lk(mu_);
            cv_.wait_for(lk, std::chrono::seconds(kFetchTimeoutSec), [&] { return filling_.count(chat) == 0; });
            lk.unlock();
            ok = pop_valid(chat, c);
        }
        if (!ok) {   // the background search was for an older song or found nothing: try once, directly
            fill(chat, last);
            ok = pop_valid(chat, c);
        }
        if (!ok) return nullptr;
        nlohmann::json j = {{"id", c.id}, {"title", c.title}, {"channel", c.channel}, {"duration", c.sec}};
        Track t = yt().make_track(j, "Autoplay", last->video);
        t.message_id = 0;
        auto p = std::make_shared<Track>(std::move(t));
        {
            std::lock_guard<std::mutex> lk(mu_);
            remember_locked(chat, p->id, c.title);
        }
        log::info("Autoplay: next song in chat " + std::to_string(chat) + ": " + p->title);
        return p;
    } catch (const std::exception& e) {
        log::warn(std::string("Autoplay next failed: ") + e.what());
        return nullptr;
    }
}

Autoplay& autoplay() {
    static Autoplay a;
    static const bool wired = [] {
        a.related_fn = [](const std::string& id) { return from_json_list(yt().related(id, 30)); };
        a.search_fn = [](const std::string& q) { return from_json_list(yt().search_many(q)); };
        return true;
    }();
    (void)wired;
    return a;
}

}  // namespace bot
