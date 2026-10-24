// Personal playlists, saved per user in the database: /addplaylist /playlist /rmplaylist (+ /playplaylist in play.cpp).
#include <algorithm>
#include "common.hpp"
#include "../core/config.hpp"
#include "../core/queue.hpp"
#include "../core/util.hpp"
#include "../db/db.hpp"
#include "../platforms/links.hpp"
#include "../platforms/yt.hpp"
#include "../tg/client.hpp"

namespace bot {
namespace {

PlItem item_of(const Track& t) {
    PlItem p;
    p.id = t.id; p.title = t.title; p.channel = t.channel_name; p.duration = t.duration; p.duration_sec = t.duration_sec;
    return p;
}

struct AddStats { int added = 0, dup = 0, full = 0; std::string first; };

void add_track(i64 user, const Track& t, AddStats& st) {
    if (t.id.empty()) return;
    int r = db().add_playlist(user, item_of(t));
    if (r == 1) { ++st.added; if (st.first.empty()) st.first = t.title; }
    else if (r == 0) ++st.dup;
    else ++st.full;
}

}  // namespace

void register_playlist() {
    register_module("playlist");
    auto& c = tg::client;

    // /addplaylist <song name | youtube url | spotify/apple/saavn/soundcloud link>   (no argument: adds the song playing now)
    on_command({"addplaylist"}, Scope::Any, Perm::Everyone, [](const Message& m, const Strings& L) {
        i64 user = m.from->id;
        std::string query = util::join(m.command, " ", 1);
        std::string url = util::url_of(m);
        AddStats st;

        if (query.empty() && url.empty()) {
            TrackPtr cur = m.chat.type == ChatType::Private ? nullptr : (db().get_call(m.chat.id) ? queue().get_current(m.chat.id) : nullptr);
            if (!cur || !cur->is_youtube) { reply(m, L["pl_add_usage"]); return; }
            add_track(user, *cur, st);
        } else {
            Message sent = reply(m, L["pl_adding"]);
            auto done = [&] {
                std::string text;
                if (st.added == 0 && st.dup == 0 && st.full == 0) text = L.f("play_not_found", {config().SUPPORT_CHAT});
                else if (st.full && !st.added) text = L.f("pl_full", {static_cast<int>(DB::kPlaylistMax)});
                else if (st.added == 0) text = L["pl_dup"];
                else if (st.added == 1) text = L.f("pl_added", {util::html_escape(st.first), static_cast<int>(db().get_playlist(user).size())});
                else text = L.f("pl_added_many", {st.added, st.dup, static_cast<int>(db().get_playlist(user).size())});
                c().edit_text(m.chat.id, sent.id, text);
            };
            if (!url.empty() && links::supported(url)) {
                auto res = links::resolve(url, config().PLAYLIST_LIMIT);
                if (res.queries.empty()) { c().edit_text(m.chat.id, sent.id, L.f("link_error", {links::source_of(url)})); return; }
                for (const auto& q : res.queries) {
                    auto t = yt().search(q, 0);
                    if (t) add_track(user, *t, st);
                    if (st.full) break;
                }
            } else if (!url.empty() && url.find("playlist") != std::string::npos && yt().valid(url)) {
                for (const auto& t : yt().playlist(config().PLAYLIST_LIMIT, m.from->mention(), url, false)) {
                    add_track(user, t, st);
                    if (st.full) break;
                }
            } else if (!url.empty() && !yt().valid(url)) {
                c().edit_text(m.chat.id, sent.id, L["pl_unsupported"]);
                return;
            } else {
                auto t = yt().search(url.empty() ? query : url, 0);
                if (t) add_track(user, *t, st);
            }
            done();
            return;
        }
        if (st.added) reply(m, L.f("pl_added", {util::html_escape(st.first), static_cast<int>(db().get_playlist(user).size())}));
        else reply(m, st.full ? L.f("pl_full", {static_cast<int>(DB::kPlaylistMax)}) : L["pl_dup"]);
    });

    // /playlist: show the caller's playlist
    on_command({"playlist"}, Scope::Any, Perm::Everyone, [](const Message& m, const Strings& L) {
        auto list = db().get_playlist(m.from->id);
        if (list.empty()) { reply(m, L["pl_empty"]); return; }
        std::string text = L.f("pl_header", {m.from->mention(), static_cast<int>(list.size())}) + "<blockquote expandable>";
        size_t shown = 0;
        for (size_t i = 0; i < list.size(); ++i) {
            std::string line = "<b>" + std::to_string(i + 1) + ".</b> " + util::html_escape(list[i].title) + " <i>(" + list[i].duration + ")</i>\n";
            if (text.size() + line.size() > 3800) break;
            text += line; ++shown;
        }
        text += "</blockquote>";
        if (shown < list.size()) text += L.f("pl_more", {static_cast<int>(list.size() - shown)});
        text += L["pl_footer"];
        reply(m, text);
    });

    // /rmplaylist <number | song name | all>
    on_command({"rmplaylist", "clearplaylist"}, Scope::Any, Perm::Everyone, [](const Message& m, const Strings& L) {
        i64 user = m.from->id;
        std::string arg = util::join(m.command, " ", 1);
        bool all = m.command[0] == "clearplaylist" || util::lower(arg) == "all";
        if (arg.empty() && !all) { reply(m, L["pl_rm_usage"]); return; }
        if (all) {
            size_t n = db().clear_playlist(user);
            reply(m, n ? L.f("pl_cleared", {static_cast<int>(n)}) : L["pl_empty"]);
            return;
        }
        auto list = db().get_playlist(user);
        if (list.empty()) { reply(m, L["pl_empty"]); return; }
        size_t pos = 0;
        if (util::is_digits(arg) && arg.size() <= 4) pos = std::stoul(arg);
        else {
            std::string needle = util::lower(arg);
            for (size_t i = 0; i < list.size(); ++i)
                if (util::lower(list[i].title).find(needle) != std::string::npos || util::lower(list[i].channel).find(needle) != std::string::npos) { pos = i + 1; break; }
        }
        if (pos < 1 || pos > list.size()) { reply(m, L["pl_rm_none"]); return; }
        std::string title = list[pos - 1].title;
        db().remove_playlist(user, pos);
        reply(m, L.f("pl_removed", {util::html_escape(title), static_cast<int>(list.size() - 1)}));
    });
}

}  // namespace bot
