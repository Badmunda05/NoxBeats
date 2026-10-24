#include "assistant.hpp"
#include <unistd.h>
#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <set>
#include "../core/log.hpp"
#include "../core/util.hpp"
#include "../mtp/crypto.hpp"

namespace bot::voice {
using nlohmann::json;
namespace fs = std::filesystem;

namespace {
constexpr i64 kChannelBase = 1000000000000LL;

std::uint64_t mtp_ping_id() { return static_cast<std::uint64_t>(std::chrono::steady_clock::now().time_since_epoch().count()) >> 1; }
json ok_() { return {{"ok", true}}; }
json fail(const std::string& code, const std::string& detail = "") { return {{"ok", false}, {"error", code}, {"detail", detail}}; }

json map_rpc(const mtp::RpcError& e) {
    const std::string& t = e.text;
    if (t == "GROUPCALL_INVALID") return fail("NO_ACTIVE_GROUP_CALL", t);
    if (t == "CHANNEL_INVALID" || t == "CHANNEL_PRIVATE" || t == "CHAT_ID_INVALID" || t == "PEER_ID_INVALID" ||
        t == "USER_NOT_PARTICIPANT" || t == "CHAT_FORBIDDEN" || t == "CHANNEL_PUBLIC_GROUP_NA")
        return fail("ASSISTANT_NOT_IN_CHAT", t);
    return fail("PLAY_FAILED", t);
}

// updateGroupCallConnection (not the presentation one) -> params.data
std::string find_connection(const json& v) {
    if (v.is_object()) {
        if (v.value("_", "") == "updateGroupCallConnection" && !v.value("presentation", false) && v.contains("params"))
            return v["params"].value("data", "");
        for (auto it = v.begin(); it != v.end(); ++it) {
            std::string r = find_connection(it.value());
            if (!r.empty()) return r;
        }
    } else if (v.is_array()) {
        for (const auto& e : v) {
            std::string r = find_connection(e);
            if (!r.empty()) return r;
        }
    }
    return "";
}

std::string strip_scheme(std::string s) {
    for (const char* p : {"https://", "http://"}) if (util::starts_with(s, p)) s = s.substr(std::strlen(p));
    for (const char* p : {"www.", "t.me/", "telegram.me/", "telegram.dog/"}) if (util::starts_with(s, p)) s = s.substr(std::strlen(p));
    return s;
}
std::string invite_hash(const std::string& link) {
    if (util::starts_with(link, "tg://join?invite=")) return link.substr(17);
    std::string s = strip_scheme(link);
    if (!s.empty() && s[0] == '+') return s.substr(1);
    if (util::starts_with(s, "joinchat/")) return s.substr(9);
    return "";
}
std::string username_of(const std::string& link) {
    std::string s = strip_scheme(link);
    if (!s.empty() && s[0] == '@') s = s.substr(1);
    size_t cut = s.find_first_of("/?#");
    if (cut != std::string::npos) s = s.substr(0, cut);
    if (s.size() < 4) return "";
    for (char c : s) if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_')) return "";
    return s;
}
}  // namespace

// ---------------------------------------------------------------- Assistant
Assistant::Assistant(int idx, const mtp::Schema& schema, mtp::SessionData s, std::uint32_t api_id, std::unique_ptr<MediaEngine> engine)
    : idx_(idx), api_id_(api_id), rpc_(schema, std::move(s), api_id), engine_(std::move(engine)) {
    engine_->on_stream_end = [this](i64 chat) { if (on_stream_end) on_stream_end(chat); };
    engine_->on_connection_lost = [this](i64 chat) {
        { std::lock_guard<std::mutex> lk(mu_); calls_.erase(chat); }
        log::warn("Assistant " + std::to_string(idx_) + ": voice connection lost in chat " + std::to_string(chat));
        if (on_chat_closed) on_chat_closed(chat);
    };
}

Assistant::~Assistant() { stop(); }

void Assistant::start() {
    rpc_.start();
    json r = rpc_.call({{"_", "users.getUsers"}, {"id", json::array({json{{"_", "inputUserSelf"}}})}}, 30);
    if (!r.is_array() || r.empty() || r[0].value("_", "") != "user") throw std::runtime_error("assistant account is not usable (revoked session?)");
    const json& u = r[0];
    user_.id = u.value("id", 0LL);
    user_.first_name = u.value("first_name", "Assistant");
    user_.username = u.value("username", "");
    user_.is_bot = u.value("bot", false);
    if (user_.is_bot) throw std::runtime_error("SESSION belongs to a bot account; an assistant must be a normal user account");
}

void Assistant::stop() {
    std::vector<i64> chats;
    {
        std::lock_guard<std::mutex> lk(mu_);
        for (auto& [c, _] : calls_) chats.push_back(c);
    }
    for (i64 c : chats) { try { do_leave(c); } catch (...) {} }
    rpc_.stop();
}

std::shared_ptr<std::mutex> Assistant::chat_lock(i64 chat) {
    std::lock_guard<std::mutex> lk(mu_);
    auto& p = locks_[chat];
    if (!p) p = std::make_shared<std::mutex>();
    return p;
}

void Assistant::learn(const json& v) {
    if (v.is_object()) {
        std::string t = v.value("_", "");
        if ((t == "channel" || t == "channelForbidden") && v.contains("id") && v.contains("access_hash")) {
            std::lock_guard<std::mutex> lk(mu_);
            channel_hash_[v["id"].get<i64>()] = v["access_hash"].get<i64>();
        }
        for (auto it = v.begin(); it != v.end(); ++it) if (it.value().is_structured()) learn(it.value());
    } else if (v.is_array()) {
        for (const auto& e : v) if (e.is_structured()) learn(e);
    }
}

std::optional<Assistant::Peer> Assistant::find_peer(i64 chat) {
    if (chat >= 0) return std::nullopt;
    if (chat <= -kChannelBase) {
        i64 id = -chat - kChannelBase;
        std::lock_guard<std::mutex> lk(mu_);
        auto it = channel_hash_.find(id);
        if (it == channel_hash_.end()) return std::nullopt;
        return Peer{true, id, it->second};
    }
    return Peer{false, -chat, 0};
}

Assistant::Peer Assistant::resolve_peer(i64 chat) {
    if (auto p = find_peer(chat)) return *p;
    scan_dialogs();
    if (auto p = find_peer(chat)) return *p;
    throw mtp::RpcError(400, "CHANNEL_INVALID");
}

json Assistant::input_channel(const Peer& p) const {
    return {{"_", "inputChannel"}, {"channel_id", p.id}, {"access_hash", p.hash}};
}

json Assistant::get_input_call(const Peer& p) {
    json r = p.channel ? rpc({{"_", "channels.getFullChannel"}, {"channel", input_channel(p)}})
                       : rpc({{"_", "messages.getFullChat"}, {"chat_id", p.id}});
    learn(r);
    const json& full = r.contains("full_chat") ? r["full_chat"] : json::object();
    if (!full.contains("call")) throw mtp::RpcError(400, "GROUPCALL_INVALID");
    return {{"_", "inputGroupCall"}, {"id", full["call"]["id"]}, {"access_hash", full["call"]["access_hash"]}};
}

std::vector<i64> Assistant::scan_dialogs() {
    std::vector<i64> out;
    std::set<i64> seen;
    json offset_peer = {{"_", "inputPeerEmpty"}};
    std::int64_t offset_date = 0, offset_id = 0;
    for (int page = 0; page < 50; ++page) {
        json r = rpc({{"_", "messages.getDialogs"}, {"offset_date", offset_date}, {"offset_id", offset_id}, {"offset_peer", offset_peer}, {"limit", 100}, {"hash", 0}}, 45);
        std::string t = r.value("_", "");
        if (t != "messages.dialogs" && t != "messages.dialogsSlice") break;
        learn(r);
        for (const auto& c : r.value("chats", json::array())) {
            std::string ct = c.value("_", "");
            if (ct == "channel" && !c.value("left", false)) {
                i64 id = -(kChannelBase + c.value("id", 0LL));
                if (seen.insert(id).second) out.push_back(id);
            } else if (ct == "chat" && !c.value("left", false) && !c.value("deactivated", false)) {
                i64 id = -c.value("id", 0LL);
                if (seen.insert(id).second) out.push_back(id);
            }
        }
        const json& dialogs = r["dialogs"];
        if (t == "messages.dialogs" || dialogs.size() < 100 || dialogs.empty()) break;
        // next page: continue after the last dialog
        const json& last = dialogs.back();
        const json& peer = last["peer"];
        std::string pt = peer.value("_", "");
        i64 top = last.value("top_message", 0LL);
        std::int64_t date = 0;
        for (const auto& m : r.value("messages", json::array()))
            if (m.value("id", 0LL) == top && m.contains("peer_id") && m["peer_id"].value("_", "") == pt) { date = m.value("date", 0LL); break; }
        if (date == 0) break;
        json ip;
        if (pt == "peerChannel") {
            i64 id = peer.value("channel_id", 0LL);
            i64 hash = 0;
            { std::lock_guard<std::mutex> lk(mu_); auto it = channel_hash_.find(id); if (it != channel_hash_.end()) hash = it->second; }
            ip = {{"_", "inputPeerChannel"}, {"channel_id", id}, {"access_hash", hash}};
        } else if (pt == "peerChat") {
            ip = {{"_", "inputPeerChat"}, {"chat_id", peer.value("chat_id", 0LL)}};
        } else {
            i64 id = peer.value("user_id", 0LL), hash = 0;
            for (const auto& u : r.value("users", json::array())) if (u.value("id", 0LL) == id) hash = u.value("access_hash", 0LL);
            ip = {{"_", "inputPeerUser"}, {"user_id", id}, {"access_hash", hash}};
        }
        offset_peer = ip;
        offset_id = top;
        offset_date = date;
    }
    return out;
}

// ---------------------------------------------------------------- operations
json Assistant::do_play(const json& b) {
    i64 chat = b.value("chat_id", 0LL);
    std::string url = b.value("url", "");
    int seek = b.value("seek", 0);
    bool video = b.value("video", false);
    double speed = b.value("speed", 1.0);
    int bass = b.value("bass", 0);
    i64 peer_chat = b.value("peer", 0LL);
    if (chat == 0 || url.empty()) return fail("MISSING_PARAMS", "chat_id and url required");
    std::string cmd, vcmd;
    try {
        cmd = build_ffmpeg_cmd(url, seek < 0 ? 0 : seek, speed, bass);
        if (video) vcmd = build_ffmpeg_video_cmd(url, seek < 0 ? 0 : seek, speed);
    }
    catch (const std::exception&) { return fail("BAD_REQUEST", "unsafe url"); }

    auto lk = chat_lock(chat);
    std::lock_guard<std::mutex> g(*lk);
    bool in_call = false;
    CallInfo stale;
    bool has_stale = false;
    {
        std::lock_guard<std::mutex> l(mu_);
        auto it = calls_.find(chat);
        if (it != calls_.end()) {
            // the video flag is part of the join request, so audio <-> video needs a fresh join
            if (it->second.video == video) in_call = true;
            else { stale = it->second; has_stale = true; calls_.erase(it); }
        }
    }
    if (has_stale) {
        try { engine_->stop(chat); } catch (...) {}
        try { rpc({{"_", "phone.leaveGroupCall"}, {"call", stale.input_call}, {"source", stale.ssrc}}, 15); } catch (...) {}
    }
    if (in_call) {
        try { engine_->set_source(chat, cmd, vcmd); return ok_(); }
        catch (const MediaError& e) {
            log::warn("Assistant " + std::to_string(idx_) + ": replacing stream failed (" + e.what() + "), rejoining");
            { std::lock_guard<std::mutex> l(mu_); calls_.erase(chat); }
        }
    }
    try { engine_->stop(chat); } catch (...) {}   // clear any half-open call from an earlier failure

    json input;
    try {
        Peer p = resolve_peer(peer_chat ? peer_chat : chat);   // channel play: the call lives in another chat
        input = get_input_call(p);
    } catch (const mtp::RpcError& e) { return map_rpc(e); }
    catch (const mtp::TransportError& e) { return fail("PLAY_FAILED", e.what()); }

    std::string params;
    try { params = engine_->create(chat); }
    catch (const MediaError& e) { return fail("PLAY_FAILED", e.what()); }
    std::int64_t ssrc = 0;
    try { ssrc = static_cast<std::int32_t>(static_cast<std::uint32_t>(json::parse(params).value("ssrc", 0ULL))); } catch (...) {}

    bool joined = false;
    json err;
    try {
        engine_->set_source(chat, cmd, vcmd);
        json r = rpc({{"_", "phone.joinGroupCall"}, {"video_stopped", !video}, {"call", input}, {"join_as", {{"_", "inputPeerSelf"}}},
                      {"params", {{"_", "dataJSON"}, {"data", params}}}}, 45);
        joined = true;
        std::string remote = find_connection(r);
        if (remote.empty()) throw MediaError(-1, "Telegram returned no voice transport");
        engine_->connect(chat, remote);
    } catch (const mtp::RpcError& e) { err = map_rpc(e); }
    catch (const mtp::TransportError& e) { err = fail("PLAY_FAILED", e.what()); }
    catch (const MediaError& e) { err = fail("PLAY_FAILED", e.what()); }
    if (!err.is_null()) {
        try { engine_->stop(chat); } catch (...) {}
        if (joined) { try { rpc({{"_", "phone.leaveGroupCall"}, {"call", input}, {"source", ssrc}}, 15); } catch (...) {} }
        return err;
    }
    { std::lock_guard<std::mutex> l(mu_); calls_[chat] = CallInfo{input, ssrc, video}; }
    log::info("Assistant " + std::to_string(idx_) + ": streaming in chat " + std::to_string(chat));
    return ok_();
}

json Assistant::do_leave(i64 chat) {
    auto lk = chat_lock(chat);
    std::lock_guard<std::mutex> g(*lk);
    CallInfo ci;
    bool had = false;
    {
        std::lock_guard<std::mutex> l(mu_);
        auto it = calls_.find(chat);
        if (it != calls_.end()) { ci = it->second; had = true; calls_.erase(it); }
    }
    try { engine_->stop(chat); } catch (...) {}
    if (had) {
        try { rpc({{"_", "phone.leaveGroupCall"}, {"call", ci.input_call}, {"source", ci.ssrc}}, 15); }
        catch (const std::exception& e) { log::warn(std::string("leaveGroupCall: ") + e.what()); }
    }
    return ok_();
}

json Assistant::do_participants(i64 chat) {
    try {
        json input;
        { std::lock_guard<std::mutex> l(mu_); auto it = calls_.find(chat); if (it != calls_.end()) input = it->second.input_call; }
        if (input.is_null()) input = get_input_call(resolve_peer(chat));
        json r = rpc({{"_", "phone.getGroupParticipants"}, {"call", input}, {"ids", json::array()}, {"sources", json::array()}, {"offset", ""}, {"limit", 1}}, 20);
        return {{"ok", true}, {"count", r.value("count", 0)}};
    } catch (const mtp::RpcError& e) { return map_rpc(e); }
    catch (const std::exception& e) { return fail("PARTICIPANTS_FAILED", e.what()); }
}

json Assistant::do_join(const json& b) {
    std::string link = b.value("invite_link", ""), uname = b.value("username", "");
    i64 chat = b.value("chat_id", 0LL);
    try {
        std::string hash = invite_hash(link);
        if (hash.empty() && !link.empty()) uname = username_of(link);
        else if (!uname.empty()) uname = username_of(uname);
        if (!hash.empty()) {
            try {
                json r = rpc({{"_", "messages.importChatInvite"}, {"hash", hash}}, 45);
                learn(r);
            } catch (const mtp::RpcError& e) {
                if (e.text == "USER_ALREADY_PARTICIPANT") {
                    try { learn(rpc({{"_", "messages.checkChatInvite"}, {"hash", hash}})); } catch (...) {}
                }
                throw;
            }
            return ok_();
        }
        if (!uname.empty()) {
            json r = rpc({{"_", "contacts.resolveUsername"}, {"username", uname}});
            learn(r);
            for (const auto& c : r.value("chats", json::array())) {
                if (c.value("_", "") != "channel") continue;
                Peer p{true, c["id"].get<i64>(), c.value("access_hash", 0LL)};
                try { learn(rpc({{"_", "channels.joinChannel"}, {"channel", input_channel(p)}}, 45)); }
                catch (const mtp::RpcError& e) { throw; }
                return ok_();
            }
            return fail("USERNAME_NOT_OCCUPIED", uname);
        }
        if (chat != 0) {
            Peer p = resolve_peer(chat);
            if (!p.channel) return ok_();
            learn(rpc({{"_", "channels.joinChannel"}, {"channel", input_channel(p)}}, 45));
            return ok_();
        }
        return fail("MISSING_PARAMS");
    } catch (const mtp::RpcError& e) {
        return fail(e.text, e.what());
    } catch (const std::exception& e) {
        return fail("JOIN_FAILED", e.what());
    }
}

json Assistant::handle(const std::string& path, const json& b) {
    try {
        if (path == "/status") {
            std::vector<i64> chats;
            { std::lock_guard<std::mutex> l(mu_); for (auto& [c, _] : calls_) chats.push_back(c); }
            return {{"ok", true}, {"assistant_id", user_.id}, {"assistant_name", user_.first_name},
                    {"assistant_username", user_.username}, {"active_chats", chats}};
        }
        if (path == "/ping") { rpc({{"_", "ping"}, {"ping_id", static_cast<std::int64_t>(mtp_ping_id())}}, 10); return ok_(); }
        if (path == "/play") return do_play(b);
        if (path == "/join") return do_join(b);
        if (path == "/leave") return do_leave(b.value("chat_id", 0LL));
        if (path == "/participants") return do_participants(b.value("chat_id", 0LL));
        if (path == "/mute" || path == "/unmute") {
            i64 chat = b.value("chat_id", 0LL);
            auto lk = chat_lock(chat);
            std::lock_guard<std::mutex> g(*lk);
            try { if (path == "/mute") engine_->mute(chat); else engine_->unmute(chat); }
            catch (const MediaError& e) { return fail("NOT_IN_CALL", e.what()); }
            return ok_();
        }
        if (path == "/pause" || path == "/resume") {
            i64 chat = b.value("chat_id", 0LL);
            auto lk = chat_lock(chat);
            std::lock_guard<std::mutex> g(*lk);
            try { if (path == "/pause") engine_->pause(chat); else engine_->resume(chat); }
            catch (const MediaError& e) { return fail("NOT_IN_CALL", e.what()); }
            return ok_();
        }
        if (path == "/send_message") {
            i64 chat = b.value("chat_id", 0LL);
            Peer p = resolve_peer(chat);
            json peer = p.channel ? json{{"_", "inputPeerChannel"}, {"channel_id", p.id}, {"access_hash", p.hash}}
                                  : json{{"_", "inputPeerChat"}, {"chat_id", p.id}};
            rpc({{"_", "messages.sendMessage"}, {"peer", peer}, {"message", b.value("text", "")},
                 {"random_id", static_cast<std::int64_t>(mtp::crypto::random_u64())}}, 30);
            return ok_();
        }
        if (path == "/dialogs") return {{"ok", true}, {"chats", scan_dialogs()}};
        if (path == "/leave_chat") {
            i64 chat = b.value("chat_id", 0LL);
            do_leave(chat);
            Peer p = resolve_peer(chat);
            if (p.channel) rpc({{"_", "channels.leaveChannel"}, {"channel", input_channel(p)}}, 30);
            else rpc({{"_", "messages.deleteChatUser"}, {"chat_id", p.id}, {"user_id", {{"_", "inputUserSelf"}}}}, 30);
            return ok_();
        }
        return fail("UNKNOWN_PATH", path);
    } catch (const mtp::RpcError& e) {
        return fail(e.text, e.what());
    } catch (const std::exception& e) {
        return fail("ERROR", e.what());
    }
}

// ---------------------------------------------------------------- manager
AssistantManager::AssistantManager(AssistantOptions o, EngineFactory f) : opt_(std::move(o)), factory_(std::move(f)) {}

bool AssistantManager::start(std::string* err) {
    auto setE = [&](const std::string& m) { if (err) *err = m; return false; };
    if (opt_.sessions.empty()) return setE("no assistant session configured");

    std::vector<std::string> dirs;
    if (!opt_.tl_dir.empty()) dirs.push_back(opt_.tl_dir);
    if (const char* e = std::getenv("TL_DIR")) dirs.push_back(e);
    dirs.push_back("telegram_schema");
    dirs.push_back("Bad/telegram_schema");
    dirs.push_back("/app/telegram_schema");
    bool loaded = false;
    for (const auto& d : dirs) {
        if (!fs::exists(fs::path(d) / "api.tl")) continue;
        try {
            schema_.load_file((fs::path(d) / "api.tl").string());
            schema_.load_file((fs::path(d) / "mtproto.tl").string());
            loaded = true;
            break;
        } catch (const std::exception& e) { return setE(e.what()); }
    }
    if (!loaded) return setE("Telegram schema (Bad/telegram_schema/api.tl, Bad/telegram_schema/mtproto.tl) not found");

    std::string override_addr = opt_.dc_override;
    if (override_addr.empty()) if (const char* e = std::getenv("MTP_DC_OVERRIDE")) override_addr = e;

    int idx = 0;
    for (const auto& s : opt_.sessions) {
        ++idx;
        auto sd = mtp::decode_session(s, opt_.session_type);
        if (!sd) return setE("SESSION " + std::to_string(idx) + " is not a valid " + opt_.session_type + " string session");
        if (sd->is_bot) return setE("SESSION " + std::to_string(idx) + " is a bot session; use a user account");
        if (!override_addr.empty()) {
            size_t c = override_addr.rfind(':');
            sd->ip = override_addr.substr(0, c);
            sd->port = std::stoi(override_addr.substr(c + 1));
            sd->fallback_ips.clear();
        }
        std::uint32_t api = static_cast<std::uint32_t>(opt_.api_id ? opt_.api_id : sd->api_id);
        std::unique_ptr<MediaEngine> eng;
        try { eng = factory_(); } catch (const std::exception& e) { return setE(std::string("voice engine: ") + e.what()); }
        auto a = std::make_unique<Assistant>(idx, schema_, *sd, api, std::move(eng));
        int i = idx;
        a->on_stream_end = [this, i](i64 chat) { if (on_stream_end) on_stream_end(i, chat); };
        a->on_chat_closed = [this, i](i64 chat) { if (on_chat_closed) on_chat_closed(i, chat); };
        try { a->start(); }
        catch (const std::exception& e) { return setE("assistant " + std::to_string(idx) + ": " + e.what()); }
        log::info("Assistant " + std::to_string(idx) + " ready: @" + a->user().username + " (" + std::to_string(a->user().id) + ")");
        list_.push_back(std::move(a));
    }
    return true;
}

void AssistantManager::stop() {
    for (auto& a : list_) a->stop();
    list_.clear();
}

json AssistantManager::call(int idx, const std::string& path, const json& body, int) {
    if (idx < 1 || idx > count()) throw std::runtime_error("no such assistant: " + std::to_string(idx));
    return list_[static_cast<size_t>(idx) - 1]->handle(path, body);
}

// ---------------------------------------------------------------- backend
AssistantBackend::AssistantBackend(AssistantManager& m) : m_(m) {
    m_.on_stream_end = [this](int, i64 chat) { if (on_stream_ended) on_stream_ended(chat); };
    m_.on_chat_closed = [this](int, i64 chat) { if (on_chat_closed) on_chat_closed(chat); };
}

void AssistantBackend::play(int idx, i64 chat, const std::string& path, bool video, int seek, double speed, i64 peer, int bass) {
    bool remote = util::starts_with(path, "http://") || util::starts_with(path, "https://");
    if (!remote && !fs::exists(path)) throw Error(Err::FileNotFound, path);
    json r;
    try { r = m_.call(idx, "/play", {{"chat_id", chat}, {"url", path}, {"video", video}, {"seek", seek > 1 ? seek : 0}, {"speed", speed}, {"peer", peer}, {"bass", bass}}, 90); }
    catch (const std::exception& e) { throw Error(Err::ConnectionNotFound, e.what()); }
    if (r.value("ok", false)) return;
    std::string code = r.value("error", "PLAY_FAILED"), detail = r.value("detail", "");
    if (code == "NO_ACTIVE_GROUP_CALL") throw Error(Err::NoActiveGroupCall, detail);
    if (code == "ASSISTANT_NOT_IN_CHAT") throw Error(Err::ConnectionNotFound, detail);
    if (code == "BAD_REQUEST") throw Error(Err::FileNotFound, "unsafe media path/url");
    throw Error(Err::Other, code + ": " + detail);
}

static void simple(AssistantManager& m, int idx, const char* path, i64 chat, bool tolerant) {
    try {
        json r = m.call(idx, path, {{"chat_id", chat}}, 30);
        if (!r.value("ok", false) && !tolerant) throw Error(Err::NotInCall, r.value("error", "failed"));
    } catch (const Error&) { throw; }
    catch (const std::exception& e) { if (!tolerant) throw Error(Err::ConnectionNotFound, e.what()); }
}

void AssistantBackend::pause(int idx, i64 chat) { simple(m_, idx, "/pause", chat, false); }
void AssistantBackend::resume(int idx, i64 chat) { simple(m_, idx, "/resume", chat, false); }
void AssistantBackend::leave(int idx, i64 chat) { simple(m_, idx, "/leave", chat, true); }
void AssistantBackend::mute(int idx, i64 chat) { simple(m_, idx, "/mute", chat, false); }
void AssistantBackend::unmute(int idx, i64 chat) { simple(m_, idx, "/unmute", chat, false); }

int AssistantBackend::participants(int idx, i64 chat) {
    try {
        json r = m_.call(idx, "/participants", {{"chat_id", chat}}, 15);
        if (r.value("ok", false)) return r.value("count", 2);
    } catch (...) {}
    return 2;   // unknown => do not auto-end
}

double AssistantBackend::ping() {
    double sum = 0;
    int n = 0;
    for (int i = 1; i <= m_.count(); ++i) {
        auto t0 = std::chrono::steady_clock::now();
        if (m_.call(i, "/ping", json::object(), 10).value("ok", false)) {
            sum += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
            ++n;
        }
    }
    return n ? sum / n : 0.0;
}

}  // namespace bot::voice
