// Channel play: /channelplay links a channel; the "c" commands (/cplay, /cskip, ...) then drive that channel's voice chat.
// The dispatcher maps the c-commands onto the normal handlers (Message::via_channel), so this file only does the linking
// and the assistant join.
#include <algorithm>
#include "common.hpp"
#include "../core/util.hpp"
#include "../db/db.hpp"
#include "../tg/client.hpp"

namespace bot {

void register_cplay() {
    register_module("cplay");

    // /channelplay <channel id | @channel | disable>
    on_command({"channelplay"}, Scope::Group, Perm::Everyone, [](const Message& m, const Strings& L) {
        auto& c = tg::client();
        i64 chat = m.chat.id;
        if (m.command.size() < 2) {
            i64 cur = db().get_cplay(chat);
            reply(m, cur ? L.f("cplay_status", {cur}) : L["cplay_usage"]);
            return;
        }
        if (!admin_check(m, L)) return;
        std::string a = m.command[1];
        std::string low = util::lower(a);
        if (low == "disable" || low == "off" || low == "none") {
            db().set_cplay(chat, 0);
            if (db().get_call(chat) && db().get_vc_channel(chat)) { /* the running session ends with /stop */ }
            reply(m, L["cplay_disabled"]);
            return;
        }
        i64 ch = 0;
        try {
            if (!a.empty() && a[0] == '@') { auto info = c.get_chat(a); if (!info) throw std::runtime_error("no chat"); ch = info->id; }
            else ch = std::stoll(a);
        } catch (...) { reply(m, L["cplay_invalid"]); return; }
        if (ch >= 0 || ch == chat) { reply(m, L["cplay_invalid"]); return; }

        // the bot must be an admin of the channel, and so must the person linking it (nobody can grab someone else's channel)
        std::vector<i64> admins;
        try { admins = c.admins(ch); }
        catch (const std::exception&) { reply(m, L["cplay_not_accessible"]); return; }
        auto has = [&](i64 id) { return std::find(admins.begin(), admins.end(), id) != admins.end(); };
        if (!has(c.me().id)) { reply(m, L["cplay_not_accessible"]); return; }
        if (!is_sudo(m.from->id) && !has(m.from->id)) { reply(m, L["cplay_not_channel_admin"]); return; }
        db().set_cplay(chat, ch);
        reply(m, L.f("cplay_enabled", {ch}));
    });

    // /userbot (and /cjoin for the linked channel): bring the assistant into the chat
    on_command({"userbot"}, Scope::Group, Perm::Everyone, [](const Message& m, const Strings& L) {
        i64 chat = m.chat.id;
        i64 target = 0;
        if (m.via_channel) {
            target = db().get_cplay(chat);
            if (!target) { reply(m, L["cplay_not_set"]); return; }
        }
        if (db().get_call(chat)) { reply(m, L["userbot_ready"]); return; }
        i64 vc = target ? target : chat;
        User ub = tg::client().assistant(db().get_assistant(chat));
        try {
            auto st = tg::client().member_status(vc, ub.id);
            if (st != tg::MemberStatus::NotMember && st != tg::MemberStatus::Banned && st != tg::MemberStatus::Restricted) { reply(m, L.f("userbot_present", {ub.mention()})); return; }
        } catch (const tg::Error& e) { if (e.kind != tg::Err::UserNotParticipant) { reply(m, L["admin_required"]); return; } }
        if (ensure_assistant(m, L, target)) reply(m, L.f("userbot_joined", {ub.mention()}));
    });
}

}  // namespace bot
