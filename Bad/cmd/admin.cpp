// Port of plugins: auth, blacklist (+ admin cache reload).
#include <ctime>
#include <map>
#include <mutex>
#include "common.hpp"
#include "../core/util.hpp"
#include "../db/db.hpp"
#include "../tg/client.hpp"

namespace bot {

void register_admin() {
    register_module("auth"); register_module("blacklist");

    on_command({"auth", "unauth"}, Scope::Group, Perm::Everyone, [](const Message& m, const Strings& L) {
        if (!admin_check(m, L)) return;
        auto user = extract_user(m);
        if (!user) { reply(m, L["user_not_found"]); return; }
        if (m.command[0] == "auth") {
            if (is_admin(m.chat.id, user->id)) { reply(m, L["auth_is_admin"]); return; }
            db().add_auth(m.chat.id, user->id);
            reply(m, L.f("auth_added", {user->mention()}));
        } else {
            db().rm_auth(m.chat.id, user->id);
            reply(m, L.f("auth_removed", {user->mention()}));
        }
    });

    on_command({"authlist"}, Scope::Group, Perm::Everyone, [](const Message& m, const Strings& L) {
        if (!admin_check(m, L)) return;
        auto auth = db().get_auth(m.chat.id);
        if (auth.empty()) { reply(m, L["auth_empty"]); return; }
        std::string t = L.f("auth_list", {util::html_escape(m.chat.title)});
        int i = 1;
        for (i64 u : auth) t += "\n" + std::to_string(i++) + ". <a href=\"tg://user?id=" + std::to_string(u) + "\">" + std::to_string(u) + "</a>";
        reply(m, t);
    });

    on_command({"admincache", "reload"}, Scope::Group, Perm::Everyone, [](const Message& m, const Strings& L) {
        static std::mutex mu;
        static std::map<i64, std::time_t> hist;
        {
            std::lock_guard<std::mutex> lk(mu);
            auto it = hist.find(m.from->id);
            if (it != hist.end() && std::time(nullptr) < it->second) { reply(m, L["admin_cache_wait"]); return; }
            hist[m.from->id] = std::time(nullptr) + 600;
        }
        Message sent = reply(m, L["admin_cache_reloading"]);
        db().get_admins(m.chat.id, true);
        tg::client().edit_text(m.chat.id, sent.id, L["admin_cache_reloaded"]);
    });

    on_command({"blacklist", "unblacklist", "whitelist"}, Scope::Any, Perm::Sudo, [](const Message& m, const Strings& L) {
        if (m.command.size() < 2) { reply(m, L.f("bl_usage", {m.command[0]})); return; }
        i64 id = 0;
        const std::string& a = m.command[1];
        try {
            if (!a.empty() && a[0] == '@') {
                auto ch = tg::client().get_chat(a);
                if (!ch) throw std::runtime_error("no chat");
                id = ch->id;
            } else {
                id = std::stoll(a);
            }
        } catch (...) { reply(m, L["bl_invalid"]); return; }

        bool is_chat = util::starts_with(std::to_string(id), "-100");
        if (m.command[0] == "blacklist") {
            if (db().is_bl_chat(id) || is_bl_user(id)) { reply(m, L["bl_already"]); return; }
            if (!is_chat) add_bl_user(id);
            db().add_blacklist(id);
            reply(m, L["bl_added"]);
        } else {
            if (!db().is_bl_chat(id) && !is_bl_user(id)) { reply(m, L["bl_not"]); return; }
            if (!is_chat) rm_bl_user(id);
            db().del_blacklist(id);
            reply(m, L["bl_removed"]);
        }
    });
}

}  // namespace bot
