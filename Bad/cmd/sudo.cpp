// Port of plugins: sudoers, active.
#include <cstdio>
#include <fstream>
#include "common.hpp"
#include "../core/config.hpp"
#include "../core/queue.hpp"
#include "../core/util.hpp"
#include "../db/db.hpp"
#include "../tg/client.hpp"

namespace bot {

void register_sudo() {
    register_module("sudoers"); register_module("active");

    on_command({"addsudo", "delsudo", "rmsudo"}, Scope::Any, Perm::Owner, [](const Message& m, const Strings& L) {
        auto user = extract_user(m);
        if (!user) { reply(m, L["user_not_found"]); return; }
        if (m.command[0] == "addsudo") {
            if (is_sudo(user->id)) { reply(m, L.f("sudo_already", {user->mention()})); return; }
            sudo_add(user->id);
            db().add_sudo(user->id);
            reply(m, L.f("sudo_added", {user->mention()}));
        } else {
            if (!is_sudo(user->id)) { reply(m, L.f("sudo_not", {user->mention()})); return; }
            sudo_del(user->id);
            db().del_sudo(user->id);
            reply(m, L.f("sudo_removed", {user->mention()}));
        }
    });

    on_command({"listsudo", "sudolist"}, Scope::Any, Perm::Everyone, [](const Message& m, const Strings& L) {
        Message sent = reply(m, L["sudo_fetching"]);
        auto& c = tg::client();
        std::string owner = "<code>" + std::to_string(config().OWNER_ID) + "</code>";
        if (auto o = c.get_user(config().OWNER_ID)) owner = o->mention();
        std::string txt = L.f("sudo_owner", {owner});
        auto sudoers = db().get_sudoers();
        if (!sudoers.empty()) txt += L["sudo_users"];
        for (i64 id : sudoers) {
            try { if (auto u = c.get_user(id)) txt += "\n- " + u->mention(); } catch (...) {}
        }
        c.edit_text(m.chat.id, sent.id, txt);
    });

    on_command({"ac", "activevc"}, Scope::Any, Perm::Sudo, [](const Message& m, const Strings& L) {
        auto calls = db().active_calls();
        if (calls.empty()) { reply(m, L["vc_empty"]); return; }
        if (m.command[0] == "ac") { reply(m, L.f("vc_count", {calls.size()})); return; }
        Message sent = reply(m, L["vc_fetching"]);
        std::string text;
        int i = 1;
        for (i64 chat : calls) {
            auto cur = queue().get_current(chat);
            text += "\n" + std::to_string(i++) + ". <code>" + std::to_string(chat) + "</code>\n    ➜ " +
                    (cur ? util::html_escape(util::truncate(cur->title, 25)) : std::string("-"));
        }
        if (text.size() < 4000) { tg::client().edit_text(m.chat.id, sent.id, L["vc_list"] + text); return; }
        { std::ofstream f("activevc.txt"); f << text; }
        tg::client().send_document(m.chat.id, "activevc.txt", L["vc_list"]);
        std::remove("activevc.txt");
    });
}

}  // namespace bot
