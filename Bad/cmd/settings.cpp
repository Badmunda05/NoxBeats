// Port of plugins: start (start/help/settings/new members), language, and the help/settings/lang callbacks.
#include <chrono>
#include <thread>
#include "common.hpp"
#include "../core/config.hpp"
#include "../core/util.hpp"
#include "../db/db.hpp"
#include "../tg/client.hpp"
#include "../ui/buttons.hpp"

namespace bot {
namespace {
std::string start_text(const Strings& L, bool priv, const User& u) {
    auto& c = tg::client();
    return priv ? L.f("start_pm", {util::html_escape(u.first_name), util::html_escape(c.me().first_name)})
                : L.f("start_gp", {util::html_escape(c.me().first_name)});
}

void help_cmd(const Message& m, const Strings& L) {
    tg::client().send_message(m.chat.id, L["help_menu"], buttons::help_markup(L), m.id);
}
}

void register_settings() {
    register_module("start"); register_module("language"); register_module("misc");
    auto& c = tg::client;

    on_command({"help"}, Scope::Private, Perm::Everyone, help_cmd);

    on_command({"start"}, Scope::Any, Perm::Everyone, [](const Message& m, const Strings& L) {
        i64 uid = m.from->id;
        if (is_bl_user(uid) && !db().notified.count(uid)) { reply(m, L["bl_user_notify"]); return; }
        if (m.command.size() > 1 && m.command[1] == "help") { help_cmd(m, L); return; }

        bool priv = m.chat.type == ChatType::Private;
        std::string text = priv ? L.f("start_pm", {util::html_escape(m.from->first_name), util::html_escape(c().me().first_name)})
                                : L.f("start_gp", {util::html_escape(c().me().first_name)});
        c().send_photo(m.chat.id, config().START_IMG, text, buttons::start_key(L, priv), priv ? 0 : m.id);
        if (priv) {
            if (db().is_user(uid)) return;
            send_log(m, L);
            db().add_user(uid);
        } else {
            if (db().is_chat(m.chat.id)) return;
            send_log(m, L, true);
            db().add_chat(m.chat.id);
        }
    });

    on_command({"playmode", "settings"}, Scope::Group, Perm::Everyone, [](const Message& m, const Strings& L) {
        i64 chat = m.chat.id;
        if (m.command[0] == "playmode" && m.command.size() > 1) {   // /playmode admin|everyone
            std::string a = util::lower(m.command[1]);
            bool admins = a == "admin" || a == "admins" || a == "adminonly", all = a == "everyone" || a == "all";
            if (!admins && !all) { reply(m, L.f("playmode_usage", {L[db().get_play_mode(chat) ? "playmode_admins" : "playmode_everyone"]})); return; }
            if (!admin_check(m, L)) return;
            db().set_play_mode(chat, all);   // remove=true -> everyone may /play
            reply(m, L.f("playmode_set", {L[admins ? "playmode_admins" : "playmode_everyone"], m.from->mention()}));
            return;
        }
        c().send_message(chat, L.f("start_settings", {util::html_escape(m.chat.title)}),
                         buttons::settings_markup(L, db().get_play_mode(chat), db().get_cmd_delete(chat), db().get_lang(chat)), m.id);
    });

    on_new_members([](const Message& m, const Strings&) {
        if (m.chat.type != ChatType::Supergroup) { tg::client().leave_chat(m.chat.id); return; }
        std::this_thread::sleep_for(std::chrono::seconds(3));
        for (const auto& u : m.new_chat_members) {
            if (u.id == tg::client().me().id) {
                if (db().is_chat(m.chat.id)) return;
                send_log(m, lang().get(db().get_lang(m.chat.id)), true);
                db().add_chat(m.chat.id);
            }
        }
    });

    // ---- language ----
    on_command({"lang", "language"}, Scope::Any, Perm::Everyone, [](const Message& m, const Strings& L) {
        reply(m, L["lang_choose"], buttons::lang_markup(db().get_lang(m.chat.id)));
    });

    on_callback(R"(^lang(?:_change|uage))", [](const CallbackQuery& q, const Strings& L) {
        if (!admin_check(q, L)) return;
        auto data = util::split_ws(q.data);
        i64 chat = q.message.chat.id;
        std::string current = db().get_lang(chat);
        if (data[0] == "language") {
            c().edit_text(chat, q.message.id, L["lang_choose"], buttons::lang_markup(current));
            return;
        }
        if (data.size() < 2 || !lang().has(data[1])) return;
        const std::string& code = data[1];
        if (current == code) { c().answer_callback(q.id, L.f("lang_same", {current}), true); return; }
        c().answer_callback(q.id, L.f("lang_change", {code}), true);
        db().set_lang(chat, code);
        c().edit_text(chat, q.message.id, L.f("lang_changed", {code}));
    });

    // ---- help ----
    on_callback("help", [](const CallbackQuery& q, const Strings& L) {
        auto data = util::split_ws(q.data);
        i64 chat = q.message.chat.id;
        c().answer_callback(q.id);
        if (data.size() == 1) {      // "Help" on the start card: turn the same card into the help menu (no /start round trip)
            c().edit_text(chat, q.message.id, L["help_menu"], buttons::help_markup(L));
            return;
        }
        if (data[1] == "home") {     // back to the start card
            bool priv = q.message.chat.type == ChatType::Private;
            c().edit_text(chat, q.message.id, start_text(L, priv, q.from), buttons::start_key(L, priv));
            return;
        }
        if (data[1] == "back") { c().edit_text(chat, q.message.id, L["help_menu"], buttons::help_markup(L)); return; }
        if (data[1] == "close") {
            try {
                c().delete_messages(chat, {q.message.id});
                if (q.message.reply_to) c().delete_messages(chat, {q.message.reply_to->id});
            } catch (...) {}
            return;
        }
        c().edit_text(chat, q.message.id, L["help_" + data[1]], buttons::help_markup(L, true));
    });

    // ---- settings ----
    on_callback("settings", [](const CallbackQuery& q, const Strings& L) {
        if (!admin_check(q, L)) return;
        auto cmd = util::split_ws(q.data);
        if (cmd.size() == 1) { c().answer_callback(q.id); return; }
        c().answer_callback(q.id, L["processing"], true);
        i64 chat = q.message.chat.id;
        bool admin = db().get_play_mode(chat), del = db().get_cmd_delete(chat);
        std::string language = db().get_lang(chat);
        if (cmd[1] == "delete") { del = !del; db().set_cmd_delete(chat, del); }
        else if (cmd[1] == "play") { db().set_play_mode(chat, admin); admin = !admin; }
        c().edit_markup(chat, q.message.id, buttons::settings_markup(L, admin, del, language));
    });
}

}  // namespace bot
