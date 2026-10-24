#include "calls.hpp"
#include "util.hpp"
#include "config.hpp"
#include "log.hpp"
#include "queue.hpp"
#include "autoplay.hpp"
#include "../db/db.hpp"
#include "../i18n/lang.hpp"
#include "../platforms/yt.hpp"
#include "../tg/client.hpp"
#include "../ui/buttons.hpp"
#include "../voice/backend.hpp"

namespace bot {

std::shared_ptr<std::recursive_mutex> chat_lock(i64 chat) {
    static std::mutex mu;
    static std::map<i64, std::shared_ptr<std::recursive_mutex>> locks;
    std::lock_guard<std::mutex> lk(mu);
    auto& p = locks[chat];
    if (!p) p = std::make_shared<std::recursive_mutex>();
    return p;
}

#define CHAT_LOCK(c) auto _l = chat_lock(c); std::lock_guard<std::recursive_mutex> _g(*_l)

static const Strings& L(i64 chat) { return lang().get(db().get_lang(chat)); }

void TgCall::pause(i64 chat) {
    CHAT_LOCK(chat);
    int idx = db().get_assistant(chat);
    db().set_playing(chat, true);
    if (TrackPtr cur = queue().get_current(chat)) cur->clock_pause();
    try { voice::backend().pause(idx, chat); }
    catch (const voice::Error& e) {
        if (e.kind == voice::Err::ConnectionNotFound || e.kind == voice::Err::NotInCall) stop(chat);
        else throw;
    }
}

void TgCall::resume(i64 chat) {
    CHAT_LOCK(chat);
    int idx = db().get_assistant(chat);
    db().set_playing(chat, false);
    if (TrackPtr cur = queue().get_current(chat)) cur->clock_resume();
    try { voice::backend().resume(idx, chat); }
    catch (const voice::Error& e) {
        if (e.kind == voice::Err::ConnectionNotFound || e.kind == voice::Err::NotInCall) stop(chat);
        else throw;
    }
}

void TgCall::mute(i64 chat) {
    CHAT_LOCK(chat);
    voice::backend().mute(db().get_assistant(chat), chat);
    db().set_muted(chat, true);
}

void TgCall::unmute(i64 chat) {
    CHAT_LOCK(chat);
    voice::backend().unmute(db().get_assistant(chat), chat);
    db().set_muted(chat, false);
}

void TgCall::stop(i64 chat) {
    CHAT_LOCK(chat);
    int idx = db().get_assistant(chat);
    queue().clear(chat);
    autoplay().reset(chat);
    db().remove_call(chat);
    db().set_loop(chat, 0);
    db().set_speed(chat, 1.0);
    db().set_bass(chat, 0);
    db().set_muted(chat, false);
    db().set_vc_channel(chat, false);
    try { voice::backend().leave(idx, chat); } catch (...) {}
}

void TgCall::play_media(i64 chat, i64 mid, const TrackPtr& media, int seek_time) {
    CHAT_LOCK(chat);
    auto& c = tg::client();
    int idx = db().get_assistant(chat);
    const Strings& lg = L(chat);
    std::string thumb;
    if (db().get_thumb(chat)) thumb = (media->is_youtube && !media->thumbnail.empty()) ? media->thumbnail : config().DEFAULT_THUMB;

    auto edit_err = [&](const std::string& text) {
        try { c.edit_text(chat, mid, text); } catch (const tg::Error&) {}
    };

    if (media->file_path.empty()) {
        edit_err(lg.f("error_no_file", {config().SUPPORT_CHAT}));
        return play_next(chat);
    }

    try {
        const double speed = db().get_speed(chat);
        const i64 peer = db().get_vc_channel(chat) ? db().get_cplay(chat) : 0;
        voice::backend().play(idx, chat, media->file_path, media->video, seek_time, speed, peer, db().get_bass(chat));
        media->clock_start(seek_time, speed);
        if (db().get_muted(chat)) { try { voice::backend().mute(idx, chat); } catch (...) {} }
        if (!seek_time) {
            media->time = 1;
            db().add_call(chat);
            autoplay().on_track_start(chat, media);
            std::string text = lg.f("play_media", {util::html_escape(media->url), util::html_escape(media->title), media->duration, media->user});
            Keyboard kb = buttons::controls(chat);
            try {
                if (!thumb.empty()) c.edit_photo(chat, mid, thumb, text, kb);
                else c.edit_text(chat, mid, text, kb);
            } catch (const tg::Error& e) {
                if (e.kind != tg::Err::ChatSendMediaForbidden && e.kind != tg::Err::MessageIdInvalid) throw;
                Message sent = !thumb.empty() ? c.send_photo(chat, thumb, text, kb) : c.send_message(chat, text, kb);
                media->message_id = sent.id;
            }
        }
    } catch (const voice::Error& e) {
        switch (e.kind) {
            case voice::Err::FileNotFound:
                edit_err(lg.f("error_no_file", {config().SUPPORT_CHAT}));
                play_next(chat);
                break;
            case voice::Err::NoActiveGroupCall:
                stop(chat);
                edit_err(lg["error_no_call"]);
                break;
            case voice::Err::NoAudioSource:
                edit_err(lg["error_no_audio"]);
                play_next(chat);
                break;
            case voice::Err::RTMPUnsupported:
                stop(chat);
                edit_err(lg["error_rtmp"]);
                break;
            case voice::Err::ConnectionNotFound:
            case voice::Err::TelegramServer:
            case voice::Err::Timeout:
                stop(chat);
                edit_err(lg["error_tg_server"]);
                break;
            default:
                log::error("play_media: " + std::string(e.what()));
                stop(chat);
                edit_err(lg["error_tg_server"]);
        }
    }
}

void TgCall::replay(i64 chat) {
    CHAT_LOCK(chat);
    if (!db().get_call(chat)) return;
    TrackPtr media = queue().get_current(chat);
    if (!media) return;
    Message msg = tg::client().send_message(chat, L(chat)["play_again"]);
    media->message_id = msg.id;
    play_media(chat, msg.id, media);
}

void TgCall::play_next(i64 chat) {
    CHAT_LOCK(chat);
    if (int loop = db().get_loop(chat)) {
        db().set_loop(chat, loop - 1);
        return replay(chat);
    }

    TrackPtr last = queue().get_current(chat);
    TrackPtr media = queue().get_next(chat);
    if (!media && last && db().get_autoplay(chat)) {
        // queue ran dry: keep going with a related song
        media = autoplay().next(chat, last);
        if (media) queue().add(chat, media);
    }
    if (!media) return stop(chat);

    if (media->message_id) {
        try { tg::client().delete_messages(chat, {media->message_id}); } catch (...) {}
        media->message_id = 0;
    }

    const Strings& lg = L(chat);
    Message msg = tg::client().send_message(chat, lg["play_next"]);
    if (media->file_path.empty()) {
        media->file_path = yt().download(media->id, media->video);
        if (media->file_path.empty()) {
            play_next(chat);
            try { tg::client().edit_text(chat, msg.id, lg.f("error_no_file", {config().SUPPORT_CHAT})); } catch (...) {}
            return;
        }
    }
    media->message_id = msg.id;
    play_media(chat, msg.id, media);
}

double TgCall::ping() { return voice::backend().ping(); }

void TgCall::boot() {
    voice::backend().on_stream_ended = [this](i64 chat) {
        try { play_next(chat); } catch (const std::exception& e) { log::error(std::string("play_next: ") + e.what()); }
    };
    voice::backend().on_chat_closed = [this](i64 chat) {
        try { stop(chat); } catch (...) {}
    };
    log::info("Voice backend wired.");
}

TgCall& bad() {
    static TgCall t;
    return t;
}

}  // namespace bot
