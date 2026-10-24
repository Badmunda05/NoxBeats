// Per-chat switches as commands: /adminmode /cmddelete /cleanmode /nothumb (the settings panel and /playmode live in settings.cpp).
#include "common.hpp"
#include "../core/util.hpp"
#include "../db/db.hpp"
#include "../tg/client.hpp"

namespace bot {
namespace {

enum class Flag { Unknown, On, Off };
Flag flag_of(const std::string& s) {
    std::string a = util::lower(s);
    if (a == "on" || a == "enable" || a == "enabled" || a == "true" || a == "yes" || a == "1") return Flag::On;
    if (a == "off" || a == "disable" || a == "disabled" || a == "false" || a == "no" || a == "0") return Flag::Off;
    return Flag::Unknown;
}

const char* mode_key(int mode) { return mode == 1 ? "adminmode_admin" : mode == 2 ? "adminmode_everyone" : "adminmode_adminauth"; }

}  // namespace

void register_chat_settings() {
    register_module("adminmode"); register_module("cleanmode");

    on_command({"adminmode"}, Scope::Group, Perm::Everyone, [](const Message& m, const Strings& L) {
        i64 chat = m.chat.id;
        if (m.command.size() < 2) { reply(m, L.f("adminmode_usage", {L[mode_key(db().get_admin_mode(chat))]})); return; }
        if (!admin_check(m, L)) return;
        std::string a = util::lower(m.command[1]);
        int mode;
        if (a == "admin" || a == "admins" || a == "adminonly") mode = 1;
        else if (a == "adminauth" || a == "auth" || a == "dj") mode = 0;
        else if (a == "everyone" || a == "all") mode = 2;
        else { reply(m, L["adminmode_invalid"]); return; }
        db().set_admin_mode(chat, mode);
        reply(m, L.f("adminmode_set", {L[mode_key(mode)], m.from->mention()}));
    });

    on_command({"cmddelete"}, Scope::Group, Perm::Everyone, [](const Message& m, const Strings& L) {
        i64 chat = m.chat.id;
        Flag f = m.command.size() < 2 ? Flag::Unknown : flag_of(m.command[1]);
        if (f == Flag::Unknown) { reply(m, L.f("cmddelete_usage", {L[db().get_cmd_delete(chat) ? "state_on" : "state_off"]})); return; }
        if (!admin_check(m, L)) return;
        db().set_cmd_delete(chat, f == Flag::On);
        reply(m, L.f("cmddelete_set", {L[f == Flag::On ? "state_on" : "state_off"], m.from->mention()}));
    });

    on_command({"cleanmode"}, Scope::Group, Perm::Everyone, [](const Message& m, const Strings& L) {
        i64 chat = m.chat.id;
        int cur = db().get_clean_mode(chat);
        if (m.command.size() < 2) { reply(m, L.f("cleanmode_usage", {L[cur ? "state_on" : "state_off"], cur})); return; }
        Flag f = flag_of(m.command[1]);
        int minutes = 0;
        if (f == Flag::Unknown && util::is_digits(m.command[1]) && m.command[1].size() <= 4) minutes = std::stoi(m.command[1]);
        if (f == Flag::Unknown && (minutes < 1 || minutes > 1440)) { reply(m, L.f("cleanmode_usage", {L[cur ? "state_on" : "state_off"], cur})); return; }
        if (!admin_check(m, L)) return;
        if (f == Flag::Off) { db().set_clean_mode(chat, 0); reply(m, L.f("cleanmode_set", {L["state_off"], m.from->mention()})); return; }
        if (f == Flag::On) minutes = cur ? cur : 15;
        if (minutes < 1) minutes = 15;
        db().set_clean_mode(chat, minutes);
        reply(m, L.f("cleanmode_on", {minutes, m.from->mention()}));
    });

    // thumbnails hidden = the opposite of /thumbnail on
    on_command({"nothumb"}, Scope::Group, Perm::Everyone, [](const Message& m, const Strings& L) {
        i64 chat = m.chat.id;
        Flag f = m.command.size() < 2 ? Flag::Unknown : flag_of(m.command[1]);
        if (f == Flag::Unknown) { reply(m, L.f("nothumb_usage", {L[db().get_thumb(chat) ? "state_off" : "state_on"]})); return; }
        if (!can_manage_vc(m, L)) return;
        db().set_thumb(chat, f == Flag::Off);   // nothumb on  -> thumbnail off
        reply(m, L.f(f == Flag::On ? "thumb_disabled" : "thumb_enabled", {m.from->mention()}));
    });
}

}  // namespace bot
