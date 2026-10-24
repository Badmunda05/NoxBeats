#include "common.hpp"
#include <algorithm>
#include <ctime>
#include <regex>
#include "../core/calls.hpp"
#include "../core/cleaner.hpp"
#include "../core/config.hpp"
#include "../core/log.hpp"
#include "../core/util.hpp"
#include "../db/db.hpp"
#include "../tg/client.hpp"

namespace bot {
namespace {

struct CmdRoute { std::vector<std::string> cmds; Scope scope; Perm perm; MsgHandler fn; };
struct CbRoute { std::regex re; CbHandler fn; };

std::vector<CmdRoute>& cmd_routes() { static std::vector<CmdRoute> r; return r; }
std::vector<CbRoute>& cb_routes() { static std::vector<CbRoute> r; return r; }
std::vector<MsgHandler>& member_routes() { static std::vector<MsgHandler> r; return r; }
std::vector<MsgHandler>& vc_routes() { static std::vector<MsgHandler> r; return r; }
std::vector<std::string>& modules() { static std::vector<std::string> r; return r; }

std::mutex g_set_mu;
std::set<i64> g_sudo, g_bl_users;

bool is_group(const Message& m) { return m.chat.type == ChatType::Group || m.chat.type == ChatType::Supergroup; }

// lang.language(): blacklist-leave + error swallowing common to every handler.
template <class F>
void guarded(i64 chat, F&& f) {
    try {
        f();
    } catch (const tg::Error& e) {
        switch (e.kind) {
            case tg::Err::FloodWait: case tg::Err::SlowmodeWait: case tg::Err::ChannelInvalid:
            case tg::Err::ChannelPrivate: case tg::Err::MessageIdInvalid: case tg::Err::MessageNotModified:
            case tg::Err::Forbidden: case tg::Err::ChatWriteForbidden:
                return;
            default:
                log::error("handler (chat " + std::to_string(chat) + "): " + e.what());
        }
    } catch (const std::exception& e) {
        log::error("handler (chat " + std::to_string(chat) + "): " + e.what());
    }
}

// "/cmd@botname args" -> ["cmd", "args"...]; empty if not a command addressed to us.
std::vector<std::string> parse_command(const std::string& text) {
    if (text.size() < 2 || text[0] != '/') return {};
    auto parts = util::split_ws(text);
    std::string head = parts[0].substr(1);
    if (auto at = head.find('@'); at != std::string::npos) {
        std::string target = util::lower(head.substr(at + 1));
        std::string me = util::lower(tg::client().me().username);
        if (target != me) return {};
        head = head.substr(0, at);
    }
    if (head.empty()) return {};
    parts[0] = util::lower(head);
    return parts;
}

// Alternative spellings -> the name the handlers are registered under.
const std::map<std::string, std::string>& aliases() {
    static const std::map<std::string, std::string> a = {
        {"p", "play"}, {"fplay", "playforce"}, {"forceplay", "playforce"}, {"fvplay", "vplayforce"}, {"vfplay", "vplayforce"},
        {"q", "queue"}, {"aplay", "autoplay"}, {"setloop", "loop"}, {"setspeed", "speed"}, {"speedup", "speed"},
        {"position", "pos"}, {"addauth", "auth"}, {"delauth", "unauth"}, {"removeauth", "unauth"},
        {"sudoers", "sudolist"}, {"listsudo", "sudolist"}, {"sudoadd", "addsudo"}, {"addsudoer", "addsudo"},
        {"delsudoer", "delsudo"}, {"sudodel", "delsudo"}, {"remsudo", "delsudo"}, {"sudorem", "delsudo"}, {"dropsudo", "delsudo"}, {"unsudo", "delsudo"},
        {"blockuser", "blacklist"}, {"blacklistuser", "blacklist"}, {"blackuser", "blacklist"}, {"bluser", "blacklist"},
        {"blockchat", "blacklist"}, {"blacklistchat", "blacklist"}, {"blackchat", "blacklist"}, {"blchat", "blacklist"},
        {"unblockuser", "unblacklist"}, {"unblacklistuser", "unblacklist"}, {"unbluser", "unblacklist"}, {"whitelistuser", "unblacklist"},
        {"unblockchat", "unblacklist"}, {"unblacklistchat", "unblacklist"}, {"unblackchat", "unblacklist"}, {"whitechat", "unblacklist"}, {"unblchat", "unblacklist"},
        {"blocked", "blacklisted"}, {"gcast", "broadcast"}, {"bcast", "broadcast"}, {"active", "ac"}, {"activevoice", "ac"},
        {"log", "logs"}, {"bash", "sh"}, {"shell", "sh"}, {"nothumbs", "nothumb"}, {"commanddelete", "cmddelete"},
        {"bassboost", "bass"}, {"listplaylist", "playlist"}, {"myplaylist", "playlist"}, {"playlists", "playlist"}, {"mylist", "playlist"},
        {"delplaylist", "rmplaylist"}, {"removeplaylist", "rmplaylist"}, {"pplaylist", "playplaylist"}, {"plplay", "playplaylist"}, {"playmylist", "playplaylist"},
        {"saveplaylist", "addplaylist"}, {"addsong", "addplaylist"}, {"maint", "maintenance"}, {"spt", "speedtest"}, {"assistant", "userbot"}, {"userbotjoin", "userbot"}, {"assistantjoin", "userbot"},
        {"join", "userbot"}, {"setcplay", "channelplay"}, {"rtmp", "setrtmp"},
    };
    return a;
}

// "c" commands run the same handler against the channel linked with /channelplay.
// first = command it maps to, second = true for the play family (those also pick the channel voice chat).
const std::map<std::string, std::pair<std::string, bool>>& channel_commands() {
    static const std::map<std::string, std::pair<std::string, bool>> a = {
        {"cplay", {"play", true}}, {"vcplay", {"vplay", true}}, {"cvplay", {"vplay", true}},
        {"cfplay", {"playforce", true}}, {"fcplay", {"playforce", true}}, {"cforceplay", {"playforce", true}}, {"cplayforce", {"playforce", true}},
        {"fvcplay", {"vplayforce", true}}, {"fvcpay", {"vplayforce", true}}, {"vcplayforce", {"vplayforce", true}},
        {"cpause", {"pause", false}}, {"cresume", {"resume", false}}, {"cmute", {"mute", false}}, {"cunmute", {"unmute", false}},
        {"cskip", {"skip", false}}, {"cstop", {"stop", false}}, {"cend", {"stop", false}}, {"cqueue", {"queue", false}},
        {"cclear", {"clear", false}}, {"cremove", {"remove", false}}, {"cmove", {"move", false}}, {"crestore", {"restore", false}},
        {"cpos", {"pos", false}}, {"cposition", {"pos", false}}, {"cseek", {"seek", false}}, {"cseekback", {"seekback", false}},
        {"cjump", {"jump", false}}, {"cbass", {"bass", false}}, {"cbassboost", {"bass", false}}, {"cspeed", {"speed", false}}, {"csetspeed", {"speed", false}}, {"cspeedup", {"speed", false}},
        {"creplay", {"replay", false}}, {"cloop", {"loop", false}}, {"csetloop", {"loop", false}}, {"cshuffle", {"shuffle", false}},
        {"cplayer", {"player", false}}, {"cjoin", {"userbot", false}}, {"creload", {"reload", false}},
    };
    return a;
}

}  // namespace

void on_command(std::vector<std::string> cmds, Scope scope, Perm perm, MsgHandler fn) {
    cmd_routes().push_back({std::move(cmds), scope, perm, std::move(fn)});
}
void on_callback(const std::string& re, CbHandler fn) { cb_routes().push_back({std::regex(re), std::move(fn)}); }
void on_new_members(MsgHandler fn) { member_routes().push_back(std::move(fn)); }
void on_video_chat(MsgHandler fn) { vc_routes().push_back(std::move(fn)); }
void register_module(const std::string& n) { modules().push_back(n); }
size_t module_count() { return modules().size(); }

// ---------- identity ----------
bool is_sudo(i64 id) {
    std::lock_guard<std::mutex> lk(g_set_mu);
    return id == config().OWNER_ID || g_sudo.count(id) > 0;
}
bool is_bl_user(i64 id) { std::lock_guard<std::mutex> lk(g_set_mu); return g_bl_users.count(id) > 0; }
void add_bl_user(i64 id) { std::lock_guard<std::mutex> lk(g_set_mu); g_bl_users.insert(id); }
void rm_bl_user(i64 id) { std::lock_guard<std::mutex> lk(g_set_mu); g_bl_users.erase(id); }
size_t sudo_count() { std::lock_guard<std::mutex> lk(g_set_mu); return g_sudo.size() + 1; }  // + owner
void load_runtime_sets() {
    std::lock_guard<std::mutex> lk(g_set_mu);
    g_sudo = db().get_sudoers();
    g_bl_users = db().bl_users();
}

// Sudo set helpers used by the sudo commands.
void sudo_add(i64 id) { std::lock_guard<std::mutex> lk(g_set_mu); g_sudo.insert(id); }
void sudo_del(i64 id) { std::lock_guard<std::mutex> lk(g_set_mu); g_sudo.erase(id); }

// ---------- dispatch ----------
void dispatch_message(const Message& m) {
    if (!m.from && !m.video_chat_started && !m.video_chat_ended && m.new_chat_members.empty()) return;
    const Strings& L = lang().get(db().get_lang(m.chat.id));

    // service messages are handled separately, so they all run
    if (!m.new_chat_members.empty())
        for (auto& h : member_routes()) guarded(m.chat.id, [&] { h(m, L); });
    if (m.video_chat_started || m.video_chat_ended)
        for (auto& h : vc_routes()) guarded(m.chat.id, [&] { h(m, L); });

    Message mm = m;
    mm.command = parse_command(m.text);
    if (mm.command.empty() || !m.from) return;
    if (auto ch = channel_commands().find(mm.command[0]); ch != channel_commands().end()) {
        mm.command[0] = ch->second.first;
        mm.via_channel = true;
    } else if (auto al = aliases().find(mm.command[0]); al != aliases().end()) {
        mm.command[0] = al->second;
    }
    const std::string c = mm.command[0];

    for (const auto& r : cmd_routes()) {
        if (std::find(r.cmds.begin(), r.cmds.end(), c) == r.cmds.end()) continue;
        if (r.scope == Scope::Group && !is_group(m)) continue;
        if (r.scope == Scope::Private && m.chat.type != ChatType::Private) continue;
        if (r.perm == Perm::Sudo && !is_sudo(m.from->id)) continue;
        if (r.perm == Perm::Owner && m.from->id != config().OWNER_ID) continue;
        // `~app.bl_users` applies to filters that had it; sudo/owner-only commands didn't need it
        if (r.perm == Perm::Everyone && c != "start" && c != "listsudo" && c != "sudolist" && is_bl_user(m.from->id)) return;

        guarded(m.chat.id, [&] {
            if (db().is_bl_chat(m.chat.id)) {
                log::info("Chat " + std::to_string(m.chat.id) + " is blacklisted, leaving...");
                tg::client().leave_chat(m.chat.id);
                return;
            }
            if (db().get_maintenance() && !is_sudo(m.from->id)) {
                std::string why = db().get_maint_reason();
                reply(m, L.f("maint", {why.empty() ? std::string() : L.f("maint_reason", {util::html_escape(why)})}));
                return;
            }
            r.fn(mm, L);
            // clean mode: the command message goes too (the bot's own replies are handled in reply())
            if (int mins = db().get_clean_mode(m.chat.id); mins > 0 && r.perm == Perm::Everyone) schedule_delete(m.chat.id, m.id, mins);
        });
        return;   // first match wins
    }
}

void dispatch_callback(const CallbackQuery& q) {
    const Strings& L = lang().get(db().get_lang(q.message.chat.id));
    for (const auto& r : cb_routes()) {
        if (!std::regex_search(q.data, r.re)) continue;
        if (is_bl_user(q.from.id)) return;
        guarded(q.message.chat.id, [&] {
            if (db().is_bl_chat(q.message.chat.id)) { tg::client().leave_chat(q.message.chat.id); return; }
            r.fn(q, L);
        });
        return;
    }
}

// ---------- permissions ----------
bool is_admin(i64 chat, i64 user) {
    auto a = db().get_admins(chat);
    if (std::find(a.begin(), a.end(), user) != a.end()) return true;
    try {
        auto s = tg::client().member_status(chat, user);
        return s == tg::MemberStatus::Admin || s == tg::MemberStatus::Owner;
    } catch (...) {
        return false;   // stop handling this update
    }
}

static bool listed_admin(i64 chat, i64 user) {
    auto a = db().get_admins(chat);
    return std::find(a.begin(), a.end(), user) != a.end();
}

bool admin_check(const Message& m, const Strings& L) {
    if (m.chat.type == ChatType::Private) return true;
    if (is_sudo(m.from->id) || listed_admin(m.chat.id, m.from->id)) return true;
    reply(m, L["user_no_perms"]);
    return false;
}
bool admin_check(const CallbackQuery& q, const Strings& L) {
    if (q.message.chat.type == ChatType::Private) return true;
    if (is_sudo(q.from.id) || listed_admin(q.message.chat.id, q.from.id)) return true;
    tg::client().answer_callback(q.id, L["user_no_perms"], true);
    return false;
}
// Who may control playback in a chat (/adminmode): 0 admins + authorized users, 1 admins only, 2 everyone.
static bool may_control(i64 chat, i64 u) {
    if (is_sudo(u)) return true;
    int mode = db().get_admin_mode(chat);
    if (mode == 2) return true;
    if (listed_admin(chat, u)) return true;
    return mode == 0 && db().is_auth(chat, u);
}

bool can_manage_vc(const Message& m, const Strings& L) {
    i64 u = m.from->id;
    if (may_control(m.chat.id, u)) return true;
    reply(m, L["user_no_perms"]);
    return false;
}
bool can_manage_vc(const CallbackQuery& q, const Strings& L) {
    i64 u = q.from.id, c = q.message.chat.id;
    if (may_control(c, u)) return true;
    tg::client().answer_callback(q.id, L["user_no_perms"], true);
    return false;
}

// ---------- helpers ----------
std::optional<User> extract_user(const Message& m) {
    if (m.reply_to && m.reply_to->from) return m.reply_to->from;
    for (const auto& e : m.entities)
        if (e.type == Entity::Type::TextMention) return e.user;
    try {
        std::smatch sm;
        if (std::regex_search(m.text, sm, std::regex(R"(@(\w{5,32}))"))) return tg::client().get_user(sm.str(0));
        if (std::regex_search(m.text, sm, std::regex(R"(\b\d{6,15}\b)"))) return tg::client().get_user(std::stoll(sm.str(0)));
    } catch (...) {}
    return std::nullopt;
}

std::string msg_link(i64 chat, i64 mid) {
    std::string c = std::to_string(chat);
    if (util::starts_with(c, "-100")) c = c.substr(4);
    return "https://t.me/c/" + c + "/" + std::to_string(mid);
}

Message reply(const Message& m, const std::string& text, const Keyboard& kb) {
    Message sent = tg::client().send_message(m.chat.id, text, kb, m.id);
    if (int mins = db().get_clean_mode(m.chat.id); mins > 0) schedule_delete(m.chat.id, sent.id, mins);
    return sent;
}

void play_log(const Message& m, const std::string& link, const std::string& title, const std::string& duration, const Strings& L) {
    if (m.chat.id == config().LOGGER_ID) return;
    auto& c = tg::client();
    c.send_message(config().LOGGER_ID,
                   L.f("play_log", {c.me().first_name, m.chat.id, util::html_escape(m.chat.title), m.from->id, m.from->mention(), link, util::html_escape(title), duration}));
}

void handle_bot_member(const BotMemberChange& c) {
    const bool gone = c.new_status == "kicked" || c.new_status == "left";
    if (!gone || c.old_status == "kicked" || c.old_status == "left") return;
    auto& cl = tg::client();
    const Strings& L = lang().get(db().get_lang(c.chat.id));
    const bool priv = c.chat.type == ChatType::Private;
    if (!priv) {
        try { if (db().get_call(c.chat.id)) bad().stop(c.chat.id); } catch (...) {}
        try { db().rm_chat(c.chat.id); } catch (...) {}
    }
    if (c.chat.id == config().LOGGER_ID || !db().is_logger()) return;
    std::string who = c.by.id ? c.by.mention() : "Unknown";
    try {
        if (priv) cl.send_message(config().LOGGER_ID, L.f("log_blocked", {c.chat.id, who}));
        else cl.send_message(config().LOGGER_ID, L.f("log_removed", {c.chat.id, util::html_escape(c.chat.title), c.by.id, who,
                                                                    c.new_status == "kicked" ? "removed (kicked)" : "left"}));
    } catch (...) {}
}

void send_log(const Message& m, const Strings& L, bool chat) {
    auto& c = tg::client();
    if (chat) {
        c.send_message(config().LOGGER_ID,
                       L.f("log_chat", {m.chat.id, util::html_escape(m.chat.title), m.from ? m.from->id : 0, m.from ? m.from->mention() : "Hidden user"}));
        return;
    }
    c.send_message(config().LOGGER_ID,
                   L.f("log_user", {m.from->id, "@" + m.from->username, m.from->mention()}));
}

}  // namespace bot
