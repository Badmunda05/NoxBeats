#include "db.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <random>
#include <nlohmann/json.hpp>
#include "../core/config.hpp"
#include "../core/log.hpp"
#include "../tg/client.hpp"

namespace bot {
using nlohmann::json;

DB::DB(std::string path, std::string mongo_uri) : path_(std::move(path)), mongo_uri_(std::move(mongo_uri)) {}

static std::set<i64> to_set(const json& j) {
    std::set<i64> s;
    if (j.is_array()) for (const auto& v : j) s.insert(v.get<i64>());
    return s;
}

void DB::apply_json(const json& j) {
    auto get = [&](const char* k) -> const json& { static const json none; auto it = j.find(k); return it == j.end() ? none : *it; };
    chats_ = to_set(get("chats")); users_ = to_set(get("users"));
    bl_chats_ = to_set(get("bl_chats")); bl_users_ = to_set(get("bl_users"));
    sudoers_ = to_set(get("sudoers")); cmd_delete_ = to_set(get("cmd_delete"));
    admin_play_ = to_set(get("admin_play"));
    logger_ = get("logger").is_boolean() && get("logger").get<bool>();
    if (get("auth").is_object()) for (auto& [k, v] : get("auth").items()) auth_[std::stoll(k)] = to_set(v);
    if (get("assistant").is_object()) for (auto& [k, v] : get("assistant").items()) assistant_[std::stoll(k)] = v.get<int>();
    if (get("lang").is_object()) for (auto& [k, v] : get("lang").items()) lang_[std::stoll(k)] = v.get<std::string>();
}

json DB::build_json() const {
    json j;
    auto arr = [](const std::set<i64>& s) { return json(s); };
    j["chats"] = arr(chats_); j["users"] = arr(users_);
    j["bl_chats"] = arr(bl_chats_); j["bl_users"] = arr(bl_users_);
    j["sudoers"] = arr(sudoers_); j["cmd_delete"] = arr(cmd_delete_); j["admin_play"] = arr(admin_play_);
    j["logger"] = logger_;
    j["auth"] = json::object(); j["assistant"] = json::object(); j["lang"] = json::object();
    for (auto& [k, v] : auth_) if (!v.empty()) j["auth"][std::to_string(k)] = arr(v);
    for (auto& [k, v] : assistant_) j["assistant"][std::to_string(k)] = v;
    for (auto& [k, v] : lang_) j["lang"][std::to_string(k)] = v;
    return j;
}

void DB::connect() {
    std::lock_guard<std::recursive_mutex> lk(mu_);
    if (!mongo_uri_.empty()) {
        mongo_ = std::make_unique<MongoStore>();
        std::string err;
        if (!mongo_->connect(mongo_uri_, &err)) { mongo_.reset(); throw std::runtime_error("MongoDB connection failed: " + err); }
        std::map<std::string, json> docs;
        if (!mongo_->load(docs, &err)) { mongo_.reset(); throw std::runtime_error("MongoDB load failed: " + err); }
        json j = json::object();
        for (auto& [k, v] : docs) { j[k] = v; saved_[k] = v.dump(); }
        try { apply_json(j); }
        catch (const std::exception& e) { throw std::runtime_error(std::string("MongoDB data is malformed: ") + e.what()); }
        log::info("MongoDB connected successfully.");
        save_locked();     // writes the keys that do not exist yet (first run)
        return;
    }
    namespace fs = std::filesystem;
    fs::create_directories(fs::path(path_).parent_path().empty() ? fs::path(".") : fs::path(path_).parent_path());
    if (!fs::exists(path_)) { save_locked(); log::info("Local database created: " + path_); return; }
    try {
        std::ifstream f(path_);
        apply_json(json::parse(f));
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string("Database load failed: ") + e.what());
    }
    log::info("Local database loaded: " + path_);
}

void DB::close() { std::lock_guard<std::recursive_mutex> lk(mu_); save_locked(); }

void DB::save_locked() {
    json j = build_json();
    if (mongo_) {
        for (auto& [k, v] : j.items()) {
            std::string dumped = v.dump();
            auto it = saved_.find(k);
            if (it != saved_.end() && it->second == dumped) continue;
            std::string err;
            if (mongo_->save(k, v, &err)) saved_[k] = std::move(dumped);
            else log::error("MongoDB save failed (" + k + "): " + err);   // retried on the next change
        }
        return;
    }
    std::string tmp = path_ + ".tmp";
    { std::ofstream f(tmp); f << j.dump(1); }
    std::error_code ec;
    std::filesystem::rename(tmp, path_, ec);
    if (ec) log::error("DB save failed: " + ec.message());
}

#define LOCK std::lock_guard<std::recursive_mutex> lk(mu_)

bool DB::get_call(i64 c) { LOCK; return active_.count(c) > 0; }
void DB::add_call(i64 c) { LOCK; active_[c] = 1; }
void DB::remove_call(i64 c) { LOCK; active_.erase(c); }
bool DB::playing(i64 c) { LOCK; auto it = active_.find(c); return it != active_.end() && it->second; }
void DB::set_playing(i64 c, bool paused) { LOCK; active_[c] = paused ? 0 : 1; }
std::vector<i64> DB::active_calls() { LOCK; std::vector<i64> v; for (auto& [k, _] : active_) v.push_back(k); return v; }
size_t DB::active_count() { LOCK; return active_.size(); }

std::vector<i64> DB::get_admins(i64 c, bool reload) {
    {
        LOCK;
        auto it = admin_list_.find(c);
        if (it != admin_list_.end() && !reload) return it->second;
    }
    std::vector<i64> a;
    try { a = tg::client().admins(c); } catch (...) {}   // reload_admins(): [] on failure
    LOCK;
    return admin_list_[c] = a;
}

int DB::get_loop(i64 c) { LOCK; auto it = loop_.find(c); return it == loop_.end() ? 0 : it->second; }
void DB::set_loop(i64 c, int n) { LOCK; loop_[c] = n; }

std::set<i64> DB::get_auth(i64 c) { LOCK; return auth_[c]; }
bool DB::is_auth(i64 c, i64 u) { LOCK; return auth_[c].count(u) > 0; }
void DB::add_auth(i64 c, i64 u) { LOCK; if (auth_[c].insert(u).second) save_locked(); }
void DB::rm_auth(i64 c, i64 u) { LOCK; if (auth_[c].erase(u)) save_locked(); }

int DB::set_assistant(i64 c) {
    LOCK;
    int n = std::max(1, tg::client().assistants());
    static std::mt19937 rng{std::random_device{}()};
    int num = std::uniform_int_distribution<int>(1, n)(rng);
    assistant_[c] = num;
    save_locked();
    return num;
}
int DB::get_assistant(i64 c) {
    LOCK;
    auto it = assistant_.find(c);
    if (it == assistant_.end() || it->second > tg::client().assistants() || it->second < 1) return set_assistant(c);
    return it->second;
}

static bool is_chat_id(i64 id) { return id < 0; }   // chat ids are negative
void DB::add_blacklist(i64 id) { LOCK; (is_chat_id(id) ? bl_chats_ : bl_users_).insert(id); save_locked(); }
void DB::del_blacklist(i64 id) { LOCK; (is_chat_id(id) ? bl_chats_ : bl_users_).erase(id); save_locked(); }
bool DB::is_bl_chat(i64 id) { LOCK; return bl_chats_.count(id) > 0; }
bool DB::is_bl_user(i64 id) { LOCK; return bl_users_.count(id) > 0; }
std::set<i64> DB::bl_chats() { LOCK; return bl_chats_; }
std::set<i64> DB::bl_users() { LOCK; return bl_users_; }

bool DB::is_chat(i64 id) { LOCK; return chats_.count(id) > 0; }
void DB::add_chat(i64 id) { LOCK; if (chats_.insert(id).second) save_locked(); }
void DB::rm_chat(i64 id) { LOCK; if (chats_.erase(id)) save_locked(); }
std::vector<i64> DB::get_chats() { LOCK; return {chats_.begin(), chats_.end()}; }
bool DB::is_user(i64 id) { LOCK; return users_.count(id) > 0; }
void DB::add_user(i64 id) { LOCK; if (users_.insert(id).second) save_locked(); }
void DB::rm_user(i64 id) { LOCK; if (users_.erase(id)) save_locked(); }
std::vector<i64> DB::get_users() { LOCK; return {users_.begin(), users_.end()}; }

bool DB::get_cmd_delete(i64 c) { LOCK; return cmd_delete_.count(c) > 0; }
void DB::set_cmd_delete(i64 c, bool v) { LOCK; if (v) cmd_delete_.insert(c); else cmd_delete_.erase(c); save_locked(); }
bool DB::get_play_mode(i64 c) { LOCK; return admin_play_.count(c) > 0; }
void DB::set_play_mode(i64 c, bool remove) { LOCK; if (remove) admin_play_.erase(c); else admin_play_.insert(c); save_locked(); }

std::string DB::get_lang(i64 c) {
    LOCK;
    auto it = lang_.find(c);
    return it == lang_.end() ? config().LANG_CODE : it->second;
}
void DB::set_lang(i64 c, const std::string& code) { LOCK; lang_[c] = code; save_locked(); }

bool DB::is_logger() { LOCK; return logger_; }
void DB::set_logger(bool v) { LOCK; logger_ = v; save_locked(); }

std::set<i64> DB::get_sudoers() { LOCK; return sudoers_; }
void DB::add_sudo(i64 id) { LOCK; sudoers_.insert(id); save_locked(); }
void DB::del_sudo(i64 id) { LOCK; sudoers_.erase(id); save_locked(); }

static std::unique_ptr<DB> g_db;
DB& db() { return *g_db; }
void init_db(const std::string& path, const std::string& mongo_uri) { g_db = std::make_unique<DB>(path, mongo_uri); }

}  // namespace bot
