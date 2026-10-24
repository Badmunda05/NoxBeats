// Autoplay: when the queue runs dry, keep the music going with songs related to the last one.
// Suggestions come from YouTube's own "up next" list, with search (and a curated artist/channel list per language and
// mood) as backup. A small buffer is filled in the background while a song plays, so the next one starts instantly.
#pragma once
#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>
#include "types.hpp"

namespace bot {

// language -> mood -> artist / channel names
using AutoplayChannels = std::map<std::string, std::map<std::string, std::vector<std::string>>>;
const AutoplayChannels& autoplay_channels();

class Autoplay {
public:
    struct Cand {
        std::string id, title, channel;   // channel is lower-case
        int sec = 0;
    };

    ~Autoplay();

    // ---- pure helpers (public for tests) ----
    static std::string extract_song_name(const std::string& title);
    static bool is_same_song(const std::string& a, const std::string& b);
    static bool is_bad_song(const std::string& title, int duration_sec);   // anything that is not one normal song
    static std::vector<Cand> filter_by_mood(const std::vector<Cand>& v, const std::string& mood);
    static std::vector<Cand> filter_by_language(const std::vector<Cand>& v, const std::string& lang);
    static const std::vector<std::string>& langs();   // "auto" + the languages with a curated list
    static const std::vector<std::string>& moods();   // "any" + the moods

    // A new song started playing: remember it and refill the suggestion buffer in the background. Never throws.
    void on_track_start(i64 chat, const TrackPtr& track);
    // The queue is empty: returns a related song (not yet downloaded) or nullptr. May block up to ~25 s.
    TrackPtr next(i64 chat, const TrackPtr& last);
    // Autoplay was switched off / the chat was stopped: drop the suggestions made so far.
    void reset(i64 chat);

    // Candidate sources, overridable for tests (default: YouTube through yt()).
    std::function<std::vector<Cand>(const std::string& video_id)> related_fn;
    std::function<std::vector<Cand>(const std::string& query)> search_fn;

private:
    std::vector<Cand> fetch_candidates(i64 chat, const TrackPtr& seed);
    std::vector<Cand> usable(const std::vector<Cand>& in, i64 chat, const std::string& seed_vid);
    std::vector<Cand> channel_candidates(const std::string& lang, const std::string& mood);
    bool is_dup(i64 chat, const std::string& id, const std::string& title);
    void remember_locked(i64 chat, const std::string& id, const std::string& title);
    void fill(i64 chat, const TrackPtr& seed);          // synchronous: search + push into the buffer
    void start_fill(i64 chat, const TrackPtr& seed);    // background thread, one per chat
    bool pop_valid(i64 chat, Cand& out);

    std::mutex mu_;
    std::condition_variable cv_;
    std::map<i64, std::deque<std::pair<std::string, std::string>>> history_;   // (id, title)
    std::map<i64, std::vector<Cand>> buffer_;
    std::set<i64> filling_;
    int active_ = 0;
};

Autoplay& autoplay();

}  // namespace bot
