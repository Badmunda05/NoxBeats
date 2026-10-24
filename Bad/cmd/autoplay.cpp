// /autoplay and /thumbnail: per-chat switches (the same switches are buttons on the player card).
#include <algorithm>
#include "common.hpp"
#include "../core/autoplay.hpp"
#include "../core/queue.hpp"
#include "../core/util.hpp"
#include "../db/db.hpp"
#include "../tg/client.hpp"

namespace bot {
namespace {

std::string codes(const std::vector<std::string>& v) {
    std::string o;
    for (const auto& x : v) o += (o.empty() ? "" : ", ") + std::string("<code>") + x + "</code>";
    return o;
}

bool one_of(const std::vector<std::string>& v, const std::string& x) { return std::find(v.begin(), v.end(), x) != v.end(); }

}  // namespace

void register_autoplay() {
    register_module("autoplay");

    on_command({"autoplay"}, Scope::Group, Perm::Everyone, [](const Message& m, const Strings& L) {
        if (!m.from) return;
        i64 chat = m.chat.id;
        std::vector<std::string> args;
        for (size_t i = 1; i < m.command.size(); ++i) args.push_back(util::lower(m.command[i]));
        std::string usage = L.f("autoplay_usage", {codes(Autoplay::langs()), codes(Autoplay::moods())});

        if (args.empty()) {   // just show the current settings
            reply(m, L.f("autoplay_status", {L[db().get_autoplay(chat) ? "autoplay_state_on" : "autoplay_state_off"],
                                             db().get_ap_lang(chat), db().get_ap_mood(chat)}) + usage);
            return;
        }
        if (!can_manage_vc(m, L)) return;
        const std::string& a = args[0];
        std::string who = m.from->mention();

        if (a == "on" || a == "enable" || a == "start") {
            db().set_autoplay(chat, true);
            if (db().get_call(chat)) autoplay().on_track_start(chat, queue().get_current(chat));
            reply(m, L.f("autoplay_enabled", {who}));
        } else if (a == "off" || a == "disable" || a == "stop") {
            db().set_autoplay(chat, false);
            autoplay().reset(chat);
            reply(m, L.f("autoplay_disabled", {who}));
        } else if ((a == "lang" || a == "language") && args.size() >= 2 && one_of(Autoplay::langs(), args[1])) {
            db().set_ap_lang(chat, args[1]);
            autoplay().reset(chat);   // suggestions made for the old language are stale
            reply(m, L.f("autoplay_lang_set", {args[1]}));
        } else if (a == "mood" && args.size() >= 2 && one_of(Autoplay::moods(), args[1])) {
            db().set_ap_mood(chat, args[1]);
            autoplay().reset(chat);
            reply(m, L.f("autoplay_mood_set", {args[1]}));
        } else {
            reply(m, usage);
        }
    });

    on_command({"thumbnail", "thumb"}, Scope::Group, Perm::Everyone, [](const Message& m, const Strings& L) {
        if (!m.from) return;
        i64 chat = m.chat.id;
        std::string a = m.command.size() > 1 ? util::lower(m.command[1]) : "";
        if (a != "on" && a != "off" && a != "enable" && a != "disable") {
            reply(m, L.f("thumb_status", {L[db().get_thumb(chat) ? "autoplay_state_on" : "autoplay_state_off"]}));
            return;
        }
        if (!can_manage_vc(m, L)) return;
        bool on = a == "on" || a == "enable";
        db().set_thumb(chat, on);
        reply(m, L.f(on ? "thumb_enabled" : "thumb_disabled", {m.from->mention()}));
    });
}

}  // namespace bot
