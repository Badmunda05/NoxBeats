// Persistent settings (sudo users, blacklist, auth lists, languages, served chats/users ...).
// State lives in memory and every mutation is written through to MongoDB (MONGO_DB_URI). Without a URI
// (tests / BOT_MOCK) it falls back to a local JSON file (DB_PATH).
#pragma once
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <vector>
#include <memory>
#include <nlohmann/json.hpp>
#include "../core/types.hpp"
#include "mongo_store.hpp"

namespace bot {

class DB {
public:
    explicit DB(std::string path, std::string mongo_uri = "");
    void connect();     // MongoDB (throws if unreachable) or, without a URI, the JSON file
    void close();

    // --- active calls (runtime only) ---
    bool get_call(i64 chat);
    void add_call(i64 chat);
    void remove_call(i64 chat);
    bool playing(i64 chat);                       // read
    void set_playing(i64 chat, bool paused);      // db.playing(chat, paused=...)
    std::vector<i64> active_calls();
    size_t active_count();

    std::vector<i64> get_admins(i64 chat, bool reload = false);

    int get_loop(i64 chat);
    void set_loop(i64 chat, int n);

    // --- auth ---
    std::set<i64> get_auth(i64 chat);
    bool is_auth(i64 chat, i64 user);
    void add_auth(i64 chat, i64 user);
    void rm_auth(i64 chat, i64 user);

    // --- assistants (1-based index) ---
    int set_assistant(i64 chat);
    int get_assistant(i64 chat);

    // --- blacklist ---
    void add_blacklist(i64 id);
    void del_blacklist(i64 id);
    bool is_bl_chat(i64 id);
    bool is_bl_user(i64 id);
    std::set<i64> bl_chats();
    std::set<i64> bl_users();

    // --- chats / users ---
    bool is_chat(i64 id);
    void add_chat(i64 id);
    void rm_chat(i64 id);
    std::vector<i64> get_chats();
    bool is_user(i64 id);
    void add_user(i64 id);
    void rm_user(i64 id);
    std::vector<i64> get_users();

    bool get_cmd_delete(i64 chat);
    void set_cmd_delete(i64 chat, bool v);
    bool get_play_mode(i64 chat);
    void set_play_mode(i64 chat, bool remove);   // remove=false -> enable

    std::string get_lang(i64 chat);
    void set_lang(i64 chat, const std::string& code);

    // --- autoplay + thumbnail (per chat, persisted) ---
    bool get_autoplay(i64 chat);
    void set_autoplay(i64 chat, bool on);
    std::string get_ap_lang(i64 chat);                 // "auto" by default
    void set_ap_lang(i64 chat, const std::string& v);
    std::string get_ap_mood(i64 chat);                 // "any" by default
    void set_ap_mood(i64 chat, const std::string& v);
    bool get_thumb(i64 chat);                          // default: THUMB_GEN
    void set_thumb(i64 chat, bool on);

    // --- more per-chat / global settings (persisted) ---
    i64 get_cplay(i64 chat);                           // linked channel for channel play, 0 = none
    void set_cplay(i64 chat, i64 channel);
    int get_admin_mode(i64 chat);                      // who may control playback: 0 admins+authorized (default), 1 admins only, 2 everyone
    void set_admin_mode(i64 chat, int mode);
    int get_clean_mode(i64 chat);                      // minutes after which the bot deletes its service messages, 0 = off
    void set_clean_mode(i64 chat, int minutes);
    bool get_autoleave();                              // default: AUTO_LEAVE
    void set_autoleave(bool on);
    bool get_maintenance();
    std::string get_maint_reason();
    void set_maintenance(bool on, const std::string& reason = "");
    std::pair<std::string, std::string> get_rtmp(i64 chat);   // (url, key); empty when not configured
    void set_rtmp(i64 chat, const std::string& url, const std::string& key);
    // --- runtime only ---
    // ---- per-user playlists (kept in the database, shared by every chat the user plays in) ----
    static constexpr size_t kPlaylistMax = 100;
    std::vector<PlItem> get_playlist(i64 user);
    int add_playlist(i64 user, const PlItem& item);       // 1 = added, 0 = already there, -1 = playlist full
    bool remove_playlist(i64 user, size_t pos);           // pos is 1-based
    size_t clear_playlist(i64 user);
    int get_bass(i64 chat);              // 0..100, 0 = off (per running session)
    void set_bass(i64 chat, int level);
    double get_speed(i64 chat);
    void set_speed(i64 chat, double s);
    bool get_muted(i64 chat);
    void set_muted(i64 chat, bool m);
    bool get_vc_channel(i64 chat);                     // this chat's session runs in the linked channel's voice chat
    void set_vc_channel(i64 chat, bool v);

    bool is_logger();
    void set_logger(bool v);

    std::set<i64> get_sudoers();
    void add_sudo(i64 id);
    void del_sudo(i64 id);

    std::set<i64> notified;   // users told about their blacklist (runtime)

private:
    void save_locked();
    void apply_json(const nlohmann::json& j);
    nlohmann::json build_json() const;
    std::string path_, mongo_uri_;
    std::unique_ptr<MongoStore> mongo_;
    std::map<std::string, std::string> saved_;   // last value written per key (only changed keys are sent to Mongo)
    std::recursive_mutex mu_;
    std::map<i64, int> active_;                  // 1 playing, 0 paused
    std::map<i64, std::vector<i64>> admin_list_;
    std::map<i64, int> loop_;
    std::map<i64, std::set<i64>> auth_;
    std::map<i64, int> assistant_;
    std::set<i64> bl_chats_, bl_users_, chats_, users_, sudoers_, cmd_delete_, admin_play_;
    std::map<i64, std::string> lang_;
    std::set<i64> autoplay_;
    std::map<i64, std::string> ap_lang_, ap_mood_;
    std::map<i64, bool> thumb_;
    std::map<i64, i64> cplay_;
    std::map<i64, int> admin_mode_, clean_mode_;
    std::map<i64, std::pair<std::string, std::string>> rtmp_;
    int autoleave_ = -1;                               // -1 = follow AUTO_LEAVE
    bool maintenance_ = false;
    std::string maint_reason_;
    std::map<i64, double> speed_;
    std::map<i64, int> bass_;
    std::map<i64, std::vector<PlItem>> playlists_;
    std::set<i64> muted_, vc_channel_;
    bool logger_ = true;
};

DB& db();
void init_db(const std::string& path, const std::string& mongo_uri = "");

}  // namespace bot
