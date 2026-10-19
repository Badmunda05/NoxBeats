// /broadcast
#include <chrono>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <set>
#include <thread>
#include "common.hpp"
#include "../core/util.hpp"
#include "../db/db.hpp"
#include "../tg/client.hpp"

namespace bot {

void register_broadcast() {
    register_module("broadcast");
    on_command({"broadcast"}, Scope::Any, Perm::Sudo, [](const Message& m, const Strings& L) {
        static std::mutex broadcasting;
        if (!m.reply_to) { reply(m, L["gcast_usage"]); return; }
        std::unique_lock<std::mutex> lk(broadcasting, std::try_to_lock);
        if (!lk.owns_lock()) { reply(m, L["gcast_active"]); return; }

        const Message& msg = *m.reply_to;
        bool copy = util::contains(m.command, "-copy");
        std::set<i64> groups, users;
        Message sent = reply(m, L["gcast_start"]);
        if (!util::contains(m.command, "-nochat")) for (i64 c : db().get_chats()) groups.insert(c);
        if (util::contains(m.command, "-user")) for (i64 u : db().get_users()) users.insert(u);

        std::set<i64> all = groups;
        all.insert(users.begin(), users.end());
        int count = 0, ucount = 0;
        std::ofstream failed;
        bool has_failed = false;
        auto& c = tg::client();
        for (i64 chat : all) {
            try {
                if (copy) c.copy_message(chat, msg); else c.forward_message(chat, msg);
                if (groups.count(chat)) ++count; else ++ucount;
                std::this_thread::sleep_for(std::chrono::milliseconds(200));
            } catch (const tg::Error& e) {
                if (e.kind == tg::Err::FloodWait) { std::this_thread::sleep_for(std::chrono::seconds(e.wait + 10)); continue; }
                if (!has_failed) { failed.open("errors.txt"); has_failed = true; }
                failed << chat << " - " << e.what() << "\n";
            } catch (const std::exception& e) {
                if (!has_failed) { failed.open("errors.txt"); has_failed = true; }
                failed << chat << " - " << e.what() << "\n";
            }
        }
        std::string text = L.f("gcast_end", {count, ucount});
        if (has_failed) {
            failed.close();
            c.send_document(m.chat.id, "errors.txt", text);
            std::remove("errors.txt");
        }
        c.edit_text(m.chat.id, sent.id, text);
    });
}

}  // namespace bot
