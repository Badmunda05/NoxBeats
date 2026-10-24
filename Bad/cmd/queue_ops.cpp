// Queue editing and extra playback controls: /clear /restore /remove /move /shuffle, /pos /jump /speed /mute /unmute /replay /player.
#include <cmath>
#include <cstdio>
#include "common.hpp"
#include "../core/calls.hpp"
#include "../core/config.hpp"
#include "../core/queue.hpp"
#include "../core/util.hpp"
#include "../db/db.hpp"
#include "../tg/client.hpp"
#include "../ui/buttons.hpp"

namespace bot {
namespace {

std::string speed_text(double s) {
    char b[16];
    std::snprintf(b, sizeof b, "%.2g", s);
    return b;
}

// "90", "1:30" or "1:02:03" -> seconds, -1 when it is not a time
int parse_time(const std::string& s) {
    if (s.empty() || s.size() > 9) return -1;
    int total = 0, cur = 0, digits = 0;
    for (char ch : s) {
        if (ch >= '0' && ch <= '9') { cur = cur * 10 + (ch - '0'); ++digits; }
        else if (ch == ':' && digits) { total = total * 60 + cur; cur = 0; digits = 0; }
        else return -1;
    }
    return digits ? total * 60 + cur : -1;
}

// the playing track, or a "nothing playing" reply
TrackPtr current_or_reply(const Message& m, const Strings& L) {
    if (!db().get_call(m.chat.id)) { reply(m, L["not_playing"]); return nullptr; }
    TrackPtr t = queue().get_current(m.chat.id);
    if (!t) reply(m, L["not_playing"]);
    return t;
}

}  // namespace

void register_queue_ops() {
    register_module("queue_ops");
    auto& c = tg::client;

    on_command({"clear"}, Scope::Group, Perm::Everyone, [](const Message& m, const Strings& L) {
        if (!can_manage_vc(m, L)) return;
        if (!db().get_call(m.chat.id)) { reply(m, L["not_playing"]); return; }
        size_t n = queue().clear_pending(m.chat.id);
        if (!n) { reply(m, L["queue_nothing_waiting"]); return; }
        reply(m, L.f("queue_cleared", {n, m.from->mention()}));
    });

    on_command({"restore"}, Scope::Group, Perm::Everyone, [](const Message& m, const Strings& L) {
        if (!can_manage_vc(m, L)) return;
        if (!db().get_call(m.chat.id)) { reply(m, L["not_playing"]); return; }
        if (!queue().restore(m.chat.id)) { reply(m, L["queue_restore_none"]); return; }
        reply(m, L.f("queue_restored", {m.from->mention()}));
    });

    on_command({"remove"}, Scope::Group, Perm::Everyone, [](const Message& m, const Strings& L) {
        if (!can_manage_vc(m, L)) return;
        if (m.command.size() < 2 || !util::is_digits(m.command[1]) || m.command[1].size() > 4) { reply(m, L["queue_remove_usage"]); return; }
        if (!db().get_call(m.chat.id)) { reply(m, L["not_playing"]); return; }
        TrackPtr gone;
        auto lock = chat_lock(m.chat.id);
        std::lock_guard<std::recursive_mutex> g(*lock);
        if (!queue().remove_at(m.chat.id, std::stoul(m.command[1]), &gone)) { reply(m, L["queue_bad_position"]); return; }
        reply(m, L.f("queue_removed", {m.command[1], util::html_escape(gone->title), m.from->mention()}));
    });

    on_command({"move"}, Scope::Group, Perm::Everyone, [](const Message& m, const Strings& L) {
        if (!can_manage_vc(m, L)) return;
        if (m.command.size() < 3 || !util::is_digits(m.command[1]) || !util::is_digits(m.command[2]) ||
            m.command[1].size() > 4 || m.command[2].size() > 4) { reply(m, L["queue_move_usage"]); return; }
        if (!db().get_call(m.chat.id)) { reply(m, L["not_playing"]); return; }
        auto lock = chat_lock(m.chat.id);
        std::lock_guard<std::recursive_mutex> g(*lock);
        if (!queue().move(m.chat.id, std::stoul(m.command[1]), std::stoul(m.command[2]))) { reply(m, L["queue_bad_position"]); return; }
        reply(m, L.f("queue_moved", {m.command[1], m.command[2], m.from->mention()}));
    });

    on_command({"shuffle"}, Scope::Group, Perm::Everyone, [](const Message& m, const Strings& L) {
        if (!can_manage_vc(m, L)) return;
        if (!db().get_call(m.chat.id)) { reply(m, L["not_playing"]); return; }
        auto lock = chat_lock(m.chat.id);
        std::lock_guard<std::recursive_mutex> g(*lock);
        size_t n = queue().get_queue(m.chat.id).size();
        if (n < 3) { reply(m, L["queue_shuffle_few"]); return; }
        queue().shuffle_pending(m.chat.id);
        reply(m, L.f("queue_shuffled", {n - 1, m.from->mention()}));
    });

    on_command({"pos"}, Scope::Group, Perm::Everyone, [](const Message& m, const Strings& L) {
        TrackPtr t = current_or_reply(m, L);
        if (!t) return;
        int pos = t->speed != 1.0 ? t->position() : t->time;
        if (t->duration_sec > 0) pos = std::min(pos, t->duration_sec);
        reply(m, L.f("position_now", {util::html_escape(t->title), util::mmss(pos), t->duration, speed_text(db().get_speed(m.chat.id))}));
    });

    on_command({"jump"}, Scope::Group, Perm::Everyone, [](const Message& m, const Strings& L) {
        if (!can_manage_vc(m, L)) return;
        int to = m.command.size() < 2 ? -1 : parse_time(m.command[1]);
        if (to < 0) { reply(m, L["jump_usage"]); return; }
        i64 chat = m.chat.id;
        if (!db().get_call(chat)) { reply(m, L["not_playing"]); return; }
        if (!db().playing(chat)) { reply(m, L["play_already_paused"]); return; }
        auto lock = chat_lock(chat);
        std::lock_guard<std::recursive_mutex> g(*lock);
        TrackPtr t = queue().get_current(chat);
        if (!t) { reply(m, L["not_playing"]); return; }
        if (!t->duration_sec) { reply(m, L["play_seek_no_dur"]); return; }
        if (to + 5 > t->duration_sec) { reply(m, L.f("jump_range", {t->duration})); return; }
        if (to < 1) to = 1;
        Message sent = reply(m, L["play_seeking"]);
        bad().play_media(chat, sent.id, t, to);
        t->time = to;
        c().edit_text(chat, sent.id, L.f("play_jumped", {util::mmss(to), m.from->mention()}));
    });

    on_command({"speed"}, Scope::Group, Perm::Everyone, [](const Message& m, const Strings& L) {
        i64 chat = m.chat.id;
        if (m.command.size() < 2) { reply(m, L.f("speed_usage", {speed_text(db().get_speed(chat))})); return; }
        if (!can_manage_vc(m, L)) return;
        std::string a = util::lower(m.command[1]);
        double s = 0;
        if (a == "normal" || a == "reset" || a == "off") s = 1.0;
        else {
            if (!a.empty() && a.back() == 'x') a.pop_back();
            char* end = nullptr;
            s = std::strtod(a.c_str(), &end);
            if (a.empty() || end == a.c_str() || *end != '\0' || !std::isfinite(s)) s = 0;
        }
        if (s < 0.5 || s > 2.0) { reply(m, L.f("speed_usage", {speed_text(db().get_speed(chat))})); return; }
        if (!db().get_call(chat)) { reply(m, L["not_playing"]); return; }
        if (!db().playing(chat)) { reply(m, L["play_already_paused"]); return; }
        auto lock = chat_lock(chat);
        std::lock_guard<std::recursive_mutex> g(*lock);
        TrackPtr t = queue().get_current(chat);
        if (!t) { reply(m, L["not_playing"]); return; }
        db().set_speed(chat, s);
        Message sent = reply(m, L["play_seeking"]);
        bad().play_media(chat, sent.id, t, std::max(1, t->time));   // restart the stream at the same spot, new speed
        c().edit_text(chat, sent.id, L.f("speed_set", {speed_text(s), m.from->mention()}));
    });

    // /bass [0-100 | on | off]: low-frequency boost for the running session (resets when the session ends)
    on_command({"bass"}, Scope::Group, Perm::Everyone, [](const Message& m, const Strings& L) {
        i64 chat = m.chat.id;
        int cur = db().get_bass(chat);
        if (m.command.size() < 2) { reply(m, L.f("bass_usage", {cur})); return; }
        if (!can_manage_vc(m, L)) return;
        std::string a = util::lower(m.command[1]);
        int lvl = -1;
        if (a == "off" || a == "reset" || a == "normal") lvl = 0;
        else if (a == "on") lvl = 50;
        else if (a == "max") lvl = 100;
        else if (util::is_digits(a) && a.size() <= 3) lvl = std::stoi(a);
        if (lvl < 0 || lvl > 100) { reply(m, L.f("bass_usage", {cur})); return; }
        if (!db().get_call(chat)) { reply(m, L["not_playing"]); return; }
        if (!db().playing(chat)) { reply(m, L["play_already_paused"]); return; }
        auto lock = chat_lock(chat);
        std::lock_guard<std::recursive_mutex> g(*lock);
        TrackPtr t = queue().get_current(chat);
        if (!t) { reply(m, L["not_playing"]); return; }
        db().set_bass(chat, lvl);
        Message sent = reply(m, L["play_seeking"]);
        bad().play_media(chat, sent.id, t, std::max(1, t->time));   // restart at the same spot with the new filter
        c().edit_text(chat, sent.id, L.f("bass_set", {lvl, m.from->mention()}));
    });

    on_command({"mute", "unmute"}, Scope::Group, Perm::Everyone, [](const Message& m, const Strings& L) {
        if (!can_manage_vc(m, L)) return;
        TrackPtr t = current_or_reply(m, L);
        if (!t) return;
        bool mute = m.command[0] == "mute";
        if (db().get_muted(m.chat.id) == mute) { reply(m, L[mute ? "mute_already" : "unmute_already"]); return; }
        try {
            if (mute) bad().mute(m.chat.id); else bad().unmute(m.chat.id);
        } catch (const std::exception&) { reply(m, L["error_tg_server"]); return; }
        reply(m, L.f(mute ? "mute_done" : "unmute_done", {util::html_escape(t->title), m.from->mention()}));
    });

    on_command({"replay"}, Scope::Group, Perm::Everyone, [](const Message& m, const Strings& L) {
        if (!can_manage_vc(m, L)) return;
        TrackPtr t = current_or_reply(m, L);
        if (!t) return;
        t->user = m.from->mention();
        bad().replay(m.chat.id);
        reply(m, L.f("play_replayed", {m.from->mention()}));
    });

    on_command({"player"}, Scope::Group, Perm::Everyone, [](const Message& m, const Strings& L) {
        i64 chat = m.chat.id;
        TrackPtr t = current_or_reply(m, L);
        if (!t) return;
        std::string text = L.f("play_media", {util::html_escape(t->url), util::html_escape(t->title), t->duration, t->user});
        Keyboard kb = buttons::controls(chat);
        Message sent;
        std::string thumb = db().get_thumb(chat) ? ((t->is_youtube && !t->thumbnail.empty()) ? t->thumbnail : config().DEFAULT_THUMB) : "";
        try { sent = thumb.empty() ? c().send_message(chat, text, kb, m.id) : c().send_photo(chat, thumb, text, kb, m.id); }
        catch (const tg::Error&) { sent = c().send_message(chat, text, kb, m.id); }
        if (t->message_id && t->message_id != sent.id) { try { c().delete_messages(chat, {t->message_id}); } catch (...) {} }
        t->message_id = sent.id;   // the progress updater now edits the new card
    });
}

}  // namespace bot
