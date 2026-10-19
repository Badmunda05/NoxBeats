// /play, /vplay, /playforce, /vplayforce and the assistant availability check.
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <thread>
#include "common.hpp"
#include "../core/calls.hpp"
#include "../core/config.hpp"
#include "../core/log.hpp"
#include "../core/queue.hpp"
#include "../core/util.hpp"
#include "../db/db.hpp"
#include "../platforms/tgfile.hpp"
#include "../platforms/yt.hpp"
#include "../tg/client.hpp"
#include "../ui/buttons.hpp"

namespace bot {
namespace {

std::string playlist_to_queue(i64 chat, const std::vector<TrackPtr>& tracks) {
    std::string text = "<blockquote expandable>";
    for (const auto& t : tracks) {
        int pos = queue().add(chat, t);
        text += "<b>" + std::to_string(pos) + ".</b> " + util::html_escape(t->title) + "\n";
    }
    if (text.size() > 1948) text.resize(1948);   // keep within Telegram's message limit
    return text + "</blockquote>";
}

bool is_assistant_ok(const Message& m, const Strings& L) {
    // checkUB: make sure the assistant is in the chat (invite it if needed)
    auto& c = tg::client();
    i64 chat = m.chat.id;
    if (db().get_call(chat)) return true;
    int idx = db().get_assistant(chat);
    User ub = c.assistant(idx);
    try {
        auto st = c.member_status(chat, ub.id);
        if (st == tg::MemberStatus::Banned || st == tg::MemberStatus::Restricted) {
            try { c.unban(chat, ub.id); }
            catch (...) {
                reply(m, L.f("play_banned", {util::html_escape(c.me().first_name), ub.id, ub.mention(), ub.username.empty() ? std::string("None") : "@" + ub.username}));
                return false;
            }
        }
        if (st != tg::MemberStatus::NotMember) return true;
    } catch (const tg::Error& e) {
        if (e.kind == tg::Err::ChatAdminRequired) { reply(m, L["admin_required"]); return false; }
        if (e.kind != tg::Err::UserNotParticipant) throw;
    }

    std::string invite;
    if (!m.chat.username.empty()) invite = m.chat.username;
    else {
        try { invite = c.invite_link(chat); }
        catch (const tg::Error& e) {
            if (e.kind == tg::Err::ChatAdminRequired) { reply(m, L["admin_required"]); return false; }
            reply(m, L.f("play_invite_error", {std::string("Error")})); return false;
        } catch (const std::exception&) {
            reply(m, L.f("play_invite_error", {std::string("Error")})); return false;
        }
    }
    Message umm = reply(m, L.f("play_invite", {util::html_escape(c.me().first_name)}));
    std::this_thread::sleep_for(std::chrono::seconds(2));
    auto fail = [&](const std::string& what) {
        c.edit_text(chat, umm.id, L.f("play_invite_error", {what}));
        return false;
    };
    try {
        c.assistant_join(idx, invite);
    } catch (const tg::Error& e) {
        if (e.kind == tg::Err::UserAlreadyParticipant) {}
        else if (e.kind == tg::Err::InviteRequestSent) {
            std::this_thread::sleep_for(std::chrono::seconds(2));
            try { c.approve_join_request(chat, ub.id); }
            catch (const tg::Error& e2) { if (e2.kind != tg::Err::HideRequesterMissing) return fail("Error"); }
        } else {
            log::error("Error joining chat - " + std::to_string(chat) + ": " + e.what());
            return fail("Error");
        }
    }
    try { c.delete_messages(chat, {umm.id}); } catch (...) {}
    return true;
}

void play_handler(const Message& m, const Strings& L, bool force, bool m3u8, bool video, const std::string& url,
                  const std::string& query) {
    auto& c = tg::client();
    i64 chat = m.chat.id;
    Message sent = reply(m, L["play_searching"]);
    std::string mention = m.from->mention();
    TrackPtr file;
    std::vector<TrackPtr> tracks;

    auto edit = [&](const std::string& t, const Keyboard& kb = {}) { c.edit_text(chat, sent.id, t, kb); };

    if (m.reply_to && TelegramFiles::get_media(*m.reply_to)) {
        auto r = tgfiles().download(*m.reply_to, chat, sent.id, L);
        if (!r) return;
        file = *r;
    } else if (m3u8) {
        file = tgfiles().process_m3u8(url, sent.id, video);
    } else if (!url.empty()) {
        if (url.find("playlist") != std::string::npos) {
            edit(L["playlist_fetch"]);
            auto list = yt().playlist(config().PLAYLIST_LIMIT, mention, url, video);
            if (list.empty()) return edit(L["playlist_error"]);
            file = std::make_shared<Track>(list[0]);
            file->message_id = sent.id;
            for (size_t i = 1; i < list.size(); ++i) tracks.push_back(std::make_shared<Track>(list[i]));
        } else {
            auto t = yt().search(url, sent.id, video);
            if (t) file = std::make_shared<Track>(*t);
        }
        if (!file) return edit(L.f("play_not_found", {config().SUPPORT_CHAT}));
    } else if (!query.empty()) {
        auto t = yt().search(query, sent.id, video);
        if (!t) return edit(L.f("play_not_found", {config().SUPPORT_CHAT}));
        file = std::make_shared<Track>(*t);
    }

    if (!file) return edit(L["play_usage"]);

    if (file->duration_sec > config().DURATION_LIMIT)
        return edit(L.f("play_duration_limit", {config().DURATION_LIMIT / 60}));

    if (db().is_logger()) {
        try { play_log(m, msg_link(chat, sent.id), file->title, file->duration, L); } catch (...) {}
    }

    file->user = mention;
    {
        auto lock = chat_lock(chat);
        std::lock_guard<std::recursive_mutex> g(*lock);
        if (force) {
            queue().force_add(chat, file);
        } else {
            int position = queue().add(chat, file);
            if (position != 0 || db().get_call(chat)) {
                edit(L.f("play_queued", {position, util::html_escape(file->url), util::html_escape(file->title), file->duration, m.from->mention()}),
                     buttons::play_queued(chat, file->id, L["play_now"]));
                if (!tracks.empty()) {
                    std::string added = playlist_to_queue(chat, tracks);
                    c.send_message(chat, L.f("playlist_queued", {tracks.size()}) + added);
                }
                return;
            }
        }
    }

    if (file->file_path.empty()) {
        std::string fname = YouTube::cached(file->id, video);
        if (!fname.empty()) file->file_path = fname;
        else {
            edit(L["play_downloading"]);
            file->file_path = yt().download(file->id, video);
        }
    }

    bad().play_media(chat, sent.id, file);
    if (tracks.empty()) return;
    std::string added = playlist_to_queue(chat, tracks);
    c.send_message(chat, L.f("playlist_queued", {tracks.size()}) + added);
}

}  // namespace

void register_play() {
    register_module("play");
    on_command({"play", "playforce", "vplay", "vplayforce"}, Scope::Group, Perm::Everyone,
               [](const Message& m, const Strings& L) {
        auto& c = tg::client();
        i64 chat = m.chat.id;
        const auto& cmd = m.command;

        if (!m.from) return reply(m, L["play_user_invalid"]), void();
        if (m.chat.type != ChatType::Supergroup) {
            reply(m, L["play_chat_invalid"]);
            c.leave_chat(chat);
            return;
        }

        // flags: -f (force)  -v (video) ; everything else is the query
        bool flag_f = false, flag_v = false;
        std::vector<std::string> words;
        for (size_t i = 1; i < cmd.size(); ++i) {
            if (cmd[i] == "-f") flag_f = true;
            else if (cmd[i] == "-v") flag_v = true;
            else words.push_back(cmd[i]);
        }
        bool has_reply = static_cast<bool>(m.reply_to);
        if (!has_reply && words.empty()) return reply(m, L["play_usage"]), void();

        if (static_cast<int>(queue().get_queue(chat).size()) >= config().QUEUE_LIMIT)
            return reply(m, L.f("play_queue_full", {config().QUEUE_LIMIT})), void();

        bool force = util::lower(cmd[0]).size() >= 5 && cmd[0].compare(cmd[0].size() - 5, 5, "force") == 0 ? true : flag_f;
        bool video = (cmd[0][0] == 'v' || flag_v) && config().VIDEO_PLAY;
        std::string url = util::url_of(m);
        if (!url.empty() && yt().invalid(url))
            return reply(m, L.f("play_not_found", {config().SUPPORT_CHAT})), void();
        bool m3u8 = !url.empty() && !yt().valid(url);

        if (db().get_play_mode(chat) || force) {
            auto admins = db().get_admins(chat);
            if (std::find(admins.begin(), admins.end(), m.from->id) == admins.end() &&
                !db().is_auth(chat, m.from->id) && !is_sudo(m.from->id))
                return reply(m, L["play_admin"]), void();
        }

        if (!is_assistant_ok(m, L)) return;

        if (db().get_cmd_delete(chat)) {
            try { c.delete_messages(chat, {m.id}); } catch (...) {}
        }
        play_handler(m, L, force, m3u8, video, url, util::join(words, " "));
    });
}

}  // namespace bot
