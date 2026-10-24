// Callbacks for the player controls and download cancel. Help/settings/lang callbacks live in settings.cpp.
#include <regex>
#include "common.hpp"
#include "../core/autoplay.hpp"
#include "../core/calls.hpp"
#include "../core/queue.hpp"
#include "../core/util.hpp"
#include "../db/db.hpp"
#include "../platforms/tgfile.hpp"
#include "../platforms/yt.hpp"
#include "../tg/client.hpp"
#include "../ui/buttons.hpp"

namespace bot {

void register_play_cb() {
    register_module("callbacks");
    auto& c = tg::client;

    on_callback("cancel_dl", [](const CallbackQuery& q, const Strings& L) {
        c().answer_callback(q.id);
        if (tgfiles().cancel(q.message.chat.id, q.message.id))
            c().edit_text(q.message.chat.id, q.message.id, L.f("dl_cancel", {q.from.mention()}));
        else
            c().answer_callback(q.id, L["dl_not_found"], true);
    });

    on_callback("controls", [](const CallbackQuery& q, const Strings& L) {
        if (!can_manage_vc(q, L)) return;
        auto args = util::split_ws(q.data);
        if (args.size() < 3 || !util::is_digits(args[2].substr(args[2][0] == '-' ? 1 : 0))) return;
        const std::string action = args[1];
        i64 chat = std::stoll(args[2]);
        bool qaction = args.size() == 4;
        std::string user = q.from.mention();
        i64 mid = q.message.id;

        if (action == "autoplay" || action == "thumb") {   // per-chat switches on the player card
            bool on;
            if (action == "autoplay") {
                on = !db().get_autoplay(chat);
                db().set_autoplay(chat, on);
                if (on) autoplay().on_track_start(chat, queue().get_current(chat));
                else autoplay().reset(chat);
            } else {
                on = !db().get_thumb(chat);
                db().set_thumb(chat, on);
            }
            c().answer_callback(q.id, L[action == "autoplay" ? (on ? "autoplay_btn_on" : "autoplay_btn_off")
                                                              : (on ? "thumb_btn_on" : "thumb_btn_off")]);
            try { c().edit_markup(q.message.chat.id, mid, buttons::controls(chat)); } catch (...) {}
            return;
        }
        if (!db().get_call(chat)) { c().answer_callback(q.id, L["not_playing"], true); return; }
        if (action == "status") { c().answer_callback(q.id); return; }
        c().answer_callback(q.id, L["processing"], true);

        std::string status, reply_text;
        if (action == "pause") {
            if (!db().playing(chat)) { c().answer_callback(q.id, L["play_already_paused"], true); return; }
            bad().pause(chat);
            if (qaction) { c().edit_markup(chat, mid, buttons::queue_markup(chat, L["paused"], false)); return; }
            status = L["paused"];
            reply_text = L.f("play_paused", {user});
        } else if (action == "resume") {
            if (db().playing(chat)) { c().answer_callback(q.id, L["play_not_paused"], true); return; }
            bad().resume(chat);
            if (qaction) { c().edit_markup(chat, mid, buttons::queue_markup(chat, L["playing"], true)); return; }
            reply_text = L.f("play_resumed", {user});
        } else if (action == "skip") {
            bad().play_next(chat);
            status = L["skipped"];
            reply_text = L.f("play_skipped", {user});
        } else if (action == "force") {
            if (args.size() < 4) return;
            auto lock = chat_lock(chat);
            std::lock_guard<std::recursive_mutex> g(*lock);
            auto [pos, media] = queue().check_item(chat, args[3]);
            if (!media || pos == -1) { c().edit_text(chat, mid, L["play_expired"]); return; }
            TrackPtr cur = queue().get_current(chat);
            i64 m_id = cur ? cur->message_id : 0;
            queue().force_add(chat, media, pos);
            try {
                std::vector<i64> ids;
                if (m_id) ids.push_back(m_id);
                if (media->message_id) ids.push_back(media->message_id);
                if (!ids.empty()) c().delete_messages(chat, ids);
                media->message_id = 0;
            } catch (...) {}
            Message msg = c().send_message(chat, L["play_next"]);
            if (media->file_path.empty()) media->file_path = yt().download(media->id, media->video);
            media->message_id = msg.id;
            bad().play_media(chat, msg.id, media);
            return;
        } else if (action == "replay") {
            TrackPtr cur = queue().get_current(chat);
            if (cur) cur->user = user;
            bad().replay(chat);
            status = L["replayed"];
            reply_text = L.f("play_replayed", {user});
        } else if (action == "stop") {
            bad().stop(chat);
            status = L["stopped"];
            reply_text = L.f("play_stopped", {user});
        } else {
            return;
        }

        try {
            if (action == "skip" || action == "replay" || action == "stop") {
                c().send_message(q.message.chat.id, reply_text);
                c().delete_messages(q.message.chat.id, {mid});
            } else {
                // strip a previous "\n\n<blockquote>...</blockquote>" status and append the new one
                static const std::regex strip(R"(\n\n<blockquote>[\s\S]*?</blockquote>)");
                std::string base = std::regex_replace(q.message.text, strip, "");
                Keyboard kb = buttons::controls(chat, action != "resume" ? status : "");
                std::string body = base + "\n\n<blockquote>" + reply_text + "</blockquote>";
                // cards created with a thumbnail are photos: edit the caption, else the text
                try { c().edit_caption(chat, mid, body, kb); }
                catch (const tg::Error&) { c().edit_text(chat, mid, body, kb); }
            }
        } catch (...) {}
    });
}

}  // namespace bot
