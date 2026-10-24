// Bot-level tools: /maintenance /autoleave /json /speedtest /bug /privacy /blacklisted.
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include "common.hpp"
#include "../core/config.hpp"
#include "../core/util.hpp"
#include "../db/db.hpp"
#include "../tg/client.hpp"

namespace bot {
namespace {

bool is_on(const std::string& s) { std::string a = util::lower(s); return a == "on" || a == "enable" || a == "enabled" || a == "true" || a == "1"; }
bool is_off(const std::string& s) { std::string a = util::lower(s); return a == "off" || a == "disable" || a == "disabled" || a == "false" || a == "0"; }

std::string fmt2(double v) { char b[32]; std::snprintf(b, sizeof b, "%.2f", v); return b; }

nlohmann::json message_json(const Message& m) {
    nlohmann::json j;
    j["message_id"] = m.id;
    j["chat"] = {{"id", m.chat.id}, {"title", m.chat.title}, {"username", m.chat.username},
                 {"type", m.chat.type == ChatType::Private ? "private" : m.chat.type == ChatType::Group ? "group" : m.chat.type == ChatType::Supergroup ? "supergroup" : "channel"}};
    if (m.from) j["from"] = {{"id", m.from->id}, {"first_name", m.from->first_name}, {"username", m.from->username}, {"is_bot", m.from->is_bot}};
    if (!m.text.empty()) j["text"] = m.text;
    if (m.has_media) {
        j["media"] = {{"file_unique_id", m.media_unique_id}, {"mime_type", m.media_mime}, {"file_name_ext", m.media_ext},
                      {"title", m.media_title}, {"size", m.media_size}, {"duration", m.media_duration}};
    }
    if (!m.link.empty()) j["link"] = m.link;
    return j;
}

// curl is already a runtime dependency (cookies, downloads): measure against Cloudflare's speed endpoints
struct Speed { bool ok = false; double down_mbps = 0, up_mbps = 0, ping_ms = 0; };
Speed run_speedtest() {
    Speed s;
    auto r = util::exec({"curl", "-s", "-o", "/dev/null", "-m", "40", "-w", "%{speed_download} %{time_connect} %{time_starttransfer}",
                         "https://speed.cloudflare.com/__down?bytes=25000000"}, 50);
    double down = 0, conn = 0, ttfb = 0;
    if (r.code != 0 || std::sscanf(r.out.c_str(), "%lf %lf %lf", &down, &conn, &ttfb) != 3 || down <= 0) return s;
    s.down_mbps = down * 8 / 1e6;
    s.ping_ms = ttfb * 1000;
    std::string tmp = "speedtest.bin";
    { std::ofstream f(tmp, std::ios::binary); std::string block(1 << 20, '\0'); for (int i = 0; i < 8; ++i) f << block; }
    auto u = util::exec({"curl", "-s", "-o", "/dev/null", "-m", "40", "-w", "%{speed_upload}", "-X", "POST", "--data-binary", "@" + tmp,
                         "https://speed.cloudflare.com/__up"}, 50);
    std::error_code ec;
    std::filesystem::remove(tmp, ec);
    double up = 0;
    if (u.code == 0 && std::sscanf(u.out.c_str(), "%lf", &up) == 1) s.up_mbps = up * 8 / 1e6;
    s.ok = true;
    return s;
}

}  // namespace

void register_bot_tools() {
    register_module("maintenance"); register_module("tools");
    auto& c = tg::client;

    on_command({"maintenance"}, Scope::Any, Perm::Owner, [](const Message& m, const Strings& L) {
        bool on = db().get_maintenance();
        std::string why = db().get_maint_reason();
        if (m.command.size() < 2) {
            reply(m, L.f("maint_usage", {L[on ? "state_on" : "state_off"], why.empty() ? std::string("-") : util::html_escape(why)}));
            return;
        }
        if (is_off(m.command[1])) {
            db().set_maintenance(false);
            reply(m, L["maint_disabled"]);
        } else if (is_on(m.command[1])) {
            std::string reason = util::join(m.command, " ", 2);
            db().set_maintenance(true, reason);
            reply(m, L.f("maint_enabled", {reason.empty() ? std::string("-") : util::html_escape(reason)}));
        } else {
            reply(m, L.f("maint_usage", {L[on ? "state_on" : "state_off"], why.empty() ? std::string("-") : util::html_escape(why)}));
        }
    });

    on_command({"autoleave"}, Scope::Any, Perm::Sudo, [](const Message& m, const Strings& L) {
        bool on = db().get_autoleave();
        if (m.command.size() < 2 || (!is_on(m.command[1]) && !is_off(m.command[1]))) { reply(m, L.f("autoleave_usage", {L[on ? "state_on" : "state_off"]})); return; }
        bool want = is_on(m.command[1]);
        db().set_autoleave(want);
        reply(m, L.f("autoleave_set", {L[want ? "state_on" : "state_off"]}));
    });

    on_command({"json"}, Scope::Any, Perm::Everyone, [](const Message& m, const Strings& L) {
        std::string out = message_json(m.reply_to ? *m.reply_to : m).dump(2);
        if (out.size() > 3500) out = out.substr(0, 3500) + "\n...";
        reply(m, "<pre><code class=\"language-json\">" + util::html_escape(out) + "</code></pre>");
    });

    on_command({"speedtest"}, Scope::Any, Perm::Sudo, [](const Message& m, const Strings& L) {
        Message sent = reply(m, L["speedtest_running"]);
        Speed s = run_speedtest();
        if (!s.ok) { c().edit_text(m.chat.id, sent.id, L["speedtest_failed"]); return; }
        c().edit_text(m.chat.id, sent.id, L.f("speedtest_result", {fmt2(s.down_mbps), fmt2(s.up_mbps), fmt2(s.ping_ms)}));
    });

    on_command({"bug"}, Scope::Any, Perm::Everyone, [](const Message& m, const Strings& L) {
        std::string text = util::join(m.command, " ", 1);
        if (text.empty() && m.reply_to) text = m.reply_to->text;
        if (text.empty()) { reply(m, L["bug_usage"]); return; }
        if (text.size() > 1500) text.resize(1500);
        std::string where = m.chat.type == ChatType::Private ? std::string("private chat") : util::html_escape(m.chat.title) + " (<code>" + std::to_string(m.chat.id) + "</code>)";
        try {
            c().send_message(config().LOGGER_ID, L.f("bug_report", {m.from->mention(), m.from->id, where, util::html_escape(text)}));
        } catch (const std::exception&) { reply(m, L["bug_failed"]); return; }
        reply(m, L["bug_sent"]);
    });

    on_command({"privacy"}, Scope::Any, Perm::Everyone, [](const Message& m, const Strings& L) {
        Keyboard kb = {{Button{L["source"], "", config().SOURCE_URL, ""}}};
        reply(m, L.f("privacy_text", {util::html_escape(c().me().first_name)}), kb);
    });

    on_command({"blacklisted"}, Scope::Any, Perm::Sudo, [](const Message& m, const Strings& L) {
        auto chats = db().bl_chats(), users = db().bl_users();
        if (chats.empty() && users.empty()) { reply(m, L["bl_empty"]); return; }
        std::string t = L["bl_list"];
        for (i64 id : chats) t += "\n- chat <code>" + std::to_string(id) + "</code>";
        for (i64 id : users) t += "\n- user <code>" + std::to_string(id) + "</code>";
        if (t.size() > 3800) t.resize(3800);
        reply(m, t);
    });
}

}  // namespace bot
