#include "common.hpp"
#include <algorithm>
#include <ctime>
#include <regex>
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

// Python: app.sudoers.add / .discard are used directly by the sudo plugin.
void sudo_add(i64 id) { std::lock_guard<std::mutex> lk(g_set_mu); g_sudo.insert(id); }
void sudo_del(i64 id) { std::lock_guard<std::mutex> lk(g_set_mu); g_sudo.erase(id); }

// ---------- dispatch ----------
void dispatch_message(const Message& m) {
    if (!m.from && !m.video_chat_started && !m.video_chat_ended && m.new_chat_members.empty()) return;
    const Strings& L = lang().get(db().get_lang(m.chat.id));

    // service messages: separate "groups" in pyrogram, so they all run
    if (!m.new_chat_members.empty())
        for (auto& h : member_routes()) guarded(m.chat.id, [&] { h(m, L); });
    if (m.video_chat_started || m.video_chat_ended)
        for (auto& h : vc_routes()) guarded(m.chat.id, [&] { h(m, L); });

    Message mm = m;
    mm.command = parse_command(m.text);
    if (mm.command.empty() || !m.from) return;
    const std::string& c = mm.command[0];

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
            r.fn(mm, L);
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
        return false;   // python: StopPropagation
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
bool can_manage_vc(const Message& m, const Strings& L) {
    i64 u = m.from->id;
    if (is_sudo(u) || db().is_auth(m.chat.id, u) || listed_admin(m.chat.id, u)) return true;
    reply(m, L["user_no_perms"]);
    return false;
}
bool can_manage_vc(const CallbackQuery& q, const Strings& L) {
    i64 u = q.from.id, c = q.message.chat.id;
    if (is_sudo(u) || db().is_auth(c, u) || listed_admin(c, u)) return true;
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
    return tg::client().send_message(m.chat.id, text, kb, m.id);
}

void play_log(const Message& m, const std::string& link, const std::string& title, const std::string& duration, const Strings& L) {
    if (m.chat.id == config().LOGGER_ID) return;
    auto& c = tg::client();
    c.send_message(config().LOGGER_ID,
                   L.f("play_log", {c.me().first_name, m.chat.id, util::html_escape(m.chat.title), m.from->id, m.from->mention(), link, util::html_escape(title), duration}));
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
