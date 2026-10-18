// Port of plugins: skip, pause, resume, stop, loop, seek, queue.
#include "common.hpp"
#include "../core/calls.hpp"
#include "../core/config.hpp"
#include "../core/queue.hpp"
#include "../core/util.hpp"
#include "../db/db.hpp"
#include "../tg/client.hpp"
#include "../ui/buttons.hpp"

namespace bot {

void register_playback() {
    register_module("skip"); register_module("pause"); register_module("resume");
    register_module("stop"); register_module("loop"); register_module("seek"); register_module("queue");

    on_command({"skip", "next"}, Scope::Group, Perm::Everyone, [](const Message& m, const Strings& L) {
        if (!can_manage_vc(m, L)) return;
        if (!db().get_call(m.chat.id)) { reply(m, L["not_playing"]); return; }
        bad().play_next(m.chat.id);
        reply(m, L.f("play_skipped", {m.from->mention()}));
    });

    on_command({"pause"}, Scope::Group, Perm::Everyone, [](const Message& m, const Strings& L) {
        if (!can_manage_vc(m, L)) return;
        i64 c = m.chat.id;
        if (!db().get_call(c)) { reply(m, L["not_playing"]); return; }
        if (!db().playing(c)) { reply(m, L["play_already_paused"]); return; }
        bad().pause(c);
        reply(m, L.f("play_paused", {m.from->mention()}), buttons::controls(c));
    });

    on_command({"resume"}, Scope::Group, Perm::Everyone, [](const Message& m, const Strings& L) {
        if (!can_manage_vc(m, L)) return;
        i64 c = m.chat.id;
        if (!db().get_call(c)) { reply(m, L["not_playing"]); return; }
        if (db().playing(c)) { reply(m, L["play_not_paused"]); return; }
        bad().resume(c);
        reply(m, L.f("play_resumed", {m.from->mention()}), buttons::controls(c));
    });

    on_command({"end", "stop"}, Scope::Group, Perm::Everyone, [](const Message& m, const Strings& L) {
        if (!can_manage_vc(m, L)) return;
        if (m.command.size() > 1) return;
        bool call = db().get_call(m.chat.id);
        bad().stop(m.chat.id);
        if (!call) { reply(m, L["not_playing"]); return; }
        reply(m, L.f("play_stopped", {m.from->mention()}));
    });

    on_command({"loop"}, Scope::Group, Perm::Everyone, [](const Message& m, const Strings& L) {
        if (!can_manage_vc(m, L)) return;
        i64 c = m.chat.id;
        if (!db().get_call(c)) { reply(m, L["not_playing"]); return; }
        if (m.command.size() < 2) {
            if (int n = db().get_loop(c)) reply(m, L.f("loop_count", {n}));
            else reply(m, L["loop_usage"]);
            return;
        }
        std::string a = util::lower(m.command[1]);
        bool disable = a == "off" || a == "disable";
        if (!util::is_digits(m.command[1]) && !disable) { reply(m, L["loop_usage"]); return; }
        long loop = disable ? 0 : std::stol(m.command[1].substr(0, 9));
        if (loop < 1) loop = 0; else if (loop > 10) loop = 10;
        db().set_loop(c, static_cast<int>(loop));
        if (loop == 0) { reply(m, L["loop_off"]); return; }
        reply(m, L.f("loop_set", {loop}));
    });

    on_command({"seek", "seekback"}, Scope::Group, Perm::Everyone, [](const Message& m, const Strings& L) {
        if (!can_manage_vc(m, L)) return;
        i64 c = m.chat.id;
        const std::string& name = m.command[0];
        if (m.command.size() < 2 || !util::is_digits(m.command[1]) || m.command[1].size() > 9) {
            reply(m, L.f("play_seek_usage", {name})); return;
        }
        int to_seek = std::stoi(m.command[1]);
        if (to_seek < 10) { reply(m, L["play_seek_min"]); return; }
        if (!db().get_call(c)) { reply(m, L["not_playing"]); return; }
        if (!db().playing(c)) { reply(m, L["play_already_paused"]); return; }
        auto lock = chat_lock(c);
        std::lock_guard<std::recursive_mutex> g(*lock);
        TrackPtr media = queue().get_current(c);
        if (!media) { reply(m, L["not_playing"]); return; }
        if (!media->duration_sec) { reply(m, L["play_seek_no_dur"]); return; }

        Message sent = reply(m, L["play_seeking"]);
        std::string stype;
        int start_from;
        if (name == "seekback") {
            stype = L["backward"];
            start_from = media->time - to_seek;
            if (start_from < 1) start_from = 1;
        } else {
            stype = L["forward"];
            start_from = media->time + to_seek;
            if (start_from + 10 > media->duration_sec) start_from = media->duration_sec - 5;
        }
        bad().play_media(c, sent.id, media, start_from);
        media->time = start_from;
        tg::client().edit_text(c, sent.id, L.f("play_seeked", {stype, start_from, m.from->mention()}));
    });

    on_command({"queue", "playing"}, Scope::Group, Perm::Everyone, [](const Message& m, const Strings& L) {
        i64 c = m.chat.id;
        if (!db().get_call(c)) { reply(m, L["not_playing"]); return; }
        Message rep = reply(m, L["queue_fetching"]);
        auto q = queue().get_queue(c);
        if (q.empty()) { tg::client().edit_text(c, rep.id, L["not_playing"]); return; }
        TrackPtr cur = q.front();
        std::string thumb;
        if (config().THUMB_GEN) thumb = (cur->is_youtube && !cur->thumbnail.empty()) ? cur->thumbnail : config().DEFAULT_THUMB;
        std::string text = L.f("queue_curr", {util::html_escape(cur->url), util::html_escape(util::truncate(cur->title, 50)), cur->duration, cur->user});
        if (q.size() > 1) {
            text += "<blockquote expandable>";
            for (size_t i = 1; i < q.size(); ++i) {
                if (i == 15) break;
                text += L.f("queue_item", {i + 1, util::html_escape(q[i]->title), q[i]->duration});
            }
            text += "</blockquote>";
        }
        bool playing = db().playing(c);
        Keyboard kb = buttons::queue_markup(c, playing ? L["playing"] : L["paused"], playing);
        if (!thumb.empty()) tg::client().edit_photo(c, rep.id, thumb, text, kb);
        else tg::client().edit_text(c, rep.id, text, kb);
    });
}

}  // namespace bot
