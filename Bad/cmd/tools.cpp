// Port of plugins: ping, stats, restart (logs/logger/restart) and misc (vc watcher + background loops).
#include <sys/statvfs.h>
#include <unistd.h>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <thread>
#include "common.hpp"
#include "../core/app.hpp"
#include "../core/calls.hpp"
#include "../core/config.hpp"
#include "../core/log.hpp"
#include "../core/queue.hpp"
#include "../core/util.hpp"
#include "../db/db.hpp"
#include "../platforms/yt.hpp"
#include "../tg/client.hpp"
#include "../ui/buttons.hpp"
#include "../voice/backend.hpp"

namespace bot {
namespace {

struct CpuTimes { unsigned long long idle = 0, total = 0; };
CpuTimes read_cpu() {
    std::ifstream f("/proc/stat");
    std::string k;
    unsigned long long v[8] = {0};
    f >> k;
    for (auto& x : v) f >> x;
    CpuTimes t;
    t.idle = v[3] + v[4];
    for (auto x : v) t.total += x;
    return t;
}
double cpu_percent(int interval_ms) {
    auto a = read_cpu();
    std::this_thread::sleep_for(std::chrono::milliseconds(interval_ms));
    auto b = read_cpu();
    double dt = static_cast<double>(b.total - a.total);
    return dt > 0 ? (1.0 - static_cast<double>(b.idle - a.idle) / dt) * 100.0 : 0.0;
}
double mem_total_gb(double* used_pct) {
    std::ifstream f("/proc/meminfo");
    std::string k, unit;
    double total = 0, avail = 0, v;
    while (f >> k >> v >> unit) {
        if (k == "MemTotal:") total = v;
        if (k == "MemAvailable:") avail = v;
    }
    if (used_pct && total > 0) *used_pct = (1.0 - avail / total) * 100.0;
    return total / (1024.0 * 1024.0);
}
double rss_mb() {
    std::ifstream f("/proc/self/statm");
    long pages = 0, rss = 0;
    f >> pages >> rss;
    return rss * static_cast<double>(sysconf(_SC_PAGESIZE)) / (1024.0 * 1024.0);
}
void disk(double* used_gb, double* total_gb, double* pct) {
    struct statvfs s{};
    if (statvfs("/", &s) != 0) { *used_gb = *total_gb = *pct = 0; return; }
    double total = static_cast<double>(s.f_blocks) * s.f_frsize, free = static_cast<double>(s.f_bfree) * s.f_frsize;
    *total_gb = total / (1024.0 * 1024 * 1024);
    *used_gb = (total - free) / (1024.0 * 1024 * 1024);
    *pct = total > 0 ? (total - free) / total * 100.0 : 0;
}
std::string f2(double v, const char* f = "%.2f") { char b[64]; std::snprintf(b, sizeof b, f, v); return b; }

std::atomic<bool> g_running{true};
std::vector<std::thread> g_threads;

void sleep_s(int s) {
    for (int i = 0; i < s * 10 && g_running; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

// track_time
void track_time() {
    while (g_running) {
        sleep_s(1);
        for (i64 chat : db().active_calls()) {
            if (!db().playing(chat)) continue;
            if (auto m = queue().get_current(chat)) m->time += 1;
        }
    }
}

// update_timer
void update_timer(int length = 10, int sleep = 12) {
    while (g_running) {
        sleep_s(sleep);
        for (i64 chat : db().active_calls()) {
            if (!g_running) return;
            if (!db().playing(chat)) continue;
            try {
                auto media = queue().get_current(chat);
                if (!media) continue;
                int duration = media->duration_sec;
                i64 mid = media->message_id;
                if (!duration || !mid || !media->time) continue;
                int played = media->time;
                int remaining = std::max(duration - played, 0);
                int pos = std::min(static_cast<int>(static_cast<double>(played) / duration * length), length - 1);
                std::string timer;
                for (int i = 0; i < length; ++i) timer += (i == pos) ? "◉" : "—";

                if (remaining <= 30) {
                    auto next = queue().get_next(chat, true);
                    if (next && next->file_path.empty()) next->file_path = yt().download(next->id, next->video);
                }
                bool remove;
                if (remaining < 10) remove = true;
                else {
                    if (config().THUMB_GEN)
                        timer = util::mmss(played) + " | " + timer + " | -" + util::mmss(remaining);
                    else timer.clear();
                    remove = false;
                }
                if (timer.empty() && !remove) continue;
                tg::client().edit_markup(chat, mid, buttons::controls(chat, "", timer, remove));
            } catch (...) {}
        }
    }
}

// vc_watcher (AUTO_END)
void vc_watcher(int sleep = 15) {
    while (g_running) {
        sleep_s(sleep);
        for (i64 chat : db().active_calls()) {
            try {
                int idx = db().get_assistant(chat);
                auto media = queue().get_current(chat);
                if (!media) continue;
                int n = voice::backend().participants(idx, chat);
                if (n < 2 && media->time > 30) {
                    const Strings& L = lang().get(db().get_lang(chat));
                    try {
                        tg::client().edit_markup(chat, media->message_id, buttons::controls(chat, L["stopped"], "", true));
                    } catch (const tg::Error&) { continue; }
                    bad().stop(chat);
                    tg::client().send_message(chat, L["auto_left"], {}, media->message_id);
                }
            } catch (...) {}
        }
    }
}

// auto_leave
void auto_leave() {
    while (g_running) {
        sleep_s(3600);
        auto& c = tg::client();
        for (int idx = 1; idx <= c.assistants(); ++idx) {
            try {
                auto chats = c.assistant_groups(idx);
                if (chats.size() > 20) chats.erase(chats.begin(), chats.end() - 20);
                for (i64 chat : chats) {
                    if (chat == config().LOGGER_ID || chat == -1001686672798LL || chat == -1001549206010LL) continue;
                    if (db().get_call(chat)) continue;
                    c.assistant_leave(idx, chat);
                    sleep_s(12);
                }
            } catch (...) {}
        }
    }
}

}  // namespace

void start_background_tasks() {
    g_running = true;
    if (config().AUTO_END) g_threads.emplace_back([] { vc_watcher(); });
    if (config().AUTO_LEAVE) g_threads.emplace_back([] { auto_leave(); });
    g_threads.emplace_back([] { track_time(); });
    g_threads.emplace_back([] { update_timer(); });
}

void stop_background_tasks() {
    g_running = false;
    for (auto& t : g_threads) if (t.joinable()) t.join();
    g_threads.clear();
}

void register_tools() {
    register_module("ping"); register_module("stats"); register_module("eval"); register_module("restart"); register_module("misc");

    on_video_chat([](const Message& m, const Strings&) { bad().stop(m.chat.id); });

    on_command({"alive", "ping"}, Scope::Any, Perm::Everyone, [](const Message& m, const Strings& L) {
        auto t0 = std::chrono::steady_clock::now();
        Message sent = reply(m, L["pinging"]);
        long up = static_cast<long>(std::time(nullptr)) - g_boot_time;
        double latency = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        double ram = 0, du, dt, dp;
        mem_total_gb(&ram);
        disk(&du, &dt, &dp);
        std::string cap = L.f("ping_pong", {f2(latency), util::uptime_string(up), f2(cpu_percent(200), "%.1f"),
                                            f2(ram, "%.1f"), f2(dp, "%.1f"), f2(bad().ping())});
        tg::client().edit_photo(m.chat.id, sent.id, config().PING_IMG, cap, buttons::ping_markup(L["support"]));
    });

    on_command({"stats"}, Scope::Any, Perm::Everyone, [](const Message& m, const Strings& L) {
        auto& c = tg::client();
        std::string text = L.f("stats_user", {util::html_escape(c.me().first_name), c.assistants(), config().AUTO_LEAVE,
                                              db().bl_chats().size(), db().bl_users().size(), sudo_count(),
                                              db().get_chats().size(), db().get_users().size()});
        if (m.from && is_sudo(m.from->id)) {
            double du, dt, dp;
            disk(&du, &dt, &dp);
            text += L.f("stats_sudo", {module_count(), "Linux", f2(rss_mb()), static_cast<long long>(mem_total_gb(nullptr) + 0.5),
                                       f2(cpu_percent(500), "%.1f"), static_cast<long long>(std::thread::hardware_concurrency()),
                                       f2(du), f2(dt), "C++20", "MTProto", "ntgcalls"});
        }
        // one request: photo + caption (no send-then-edit round trip that can fail half way); plain text as a fallback
        try {
            c.send_photo(m.chat.id, config().PING_IMG, text, {}, m.id);
        } catch (const std::exception& e) {
            log::warn(std::string("/stats photo failed: ") + e.what());
            reply(m, text);
        }
    });

    // Owner-only remote shell. /eval and /sh both run the text after the command through `sh -c`.
    on_command({"eval", "ev", "sh", "exec"}, Scope::Any, Perm::Owner, [](const Message& m, const Strings&) {
        std::string cmd;
        size_t sp = m.text.find_first_of(" \n");
        if (sp != std::string::npos) cmd = m.text.substr(sp + 1);
        if (cmd.empty() && m.reply_to) cmd = m.reply_to->text;
        while (!cmd.empty() && (cmd.back() == '\n' || cmd.back() == ' ')) cmd.pop_back();
        if (cmd.empty()) { reply(m, "Usage: <code>/eval &lt;shell command&gt;</code>"); return; }
        Message sent = reply(m, "Running...");
        auto t0 = std::chrono::steady_clock::now();
        auto r = util::exec({"/bin/sh", "-c", cmd}, 60);
        double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
        std::string out = r.out + r.err;
        if (out.size() > 3000) out = "..." + out.substr(out.size() - 3000);
        if (out.empty()) out = "(no output)";
        char tail[96];
        std::snprintf(tail, sizeof tail, "\n<b>exit:</b> <code>%d</code> | <code>%.2fs</code>", r.code, secs);
        tg::client().edit_text(m.chat.id, sent.id, "<b>Input:</b>\n<pre>" + util::html_escape(cmd.size() > 800 ? cmd.substr(0, 800) + "..." : cmd) +
                                                        "</pre>\n<b>Output:</b>\n<pre>" + util::html_escape(out) + "</pre>" + tail);
    });

    on_command({"logs"}, Scope::Any, Perm::Sudo, [](const Message& m, const Strings& L) {
        Message sent = reply(m, L["log_fetch"]);
        if (!std::filesystem::exists("log.txt")) { tg::client().edit_text(m.chat.id, sent.id, L["log_not_found"]); return; }
        tg::client().send_document(m.chat.id, "log.txt", L.f("log_sent", {util::html_escape(tg::client().me().first_name)}));
    });

    on_command({"logger"}, Scope::Any, Perm::Sudo, [](const Message& m, const Strings& L) {
        if (m.command.size() < 2 || (m.command[1] != "on" && m.command[1] != "off")) {
            reply(m, L.f("logger_usage", {m.command[0]})); return;
        }
        bool on = m.command[1] == "on";
        db().set_logger(on);
        reply(m, L[on ? "logger_on" : "logger_off"]);
    });

    on_command({"restart"}, Scope::Any, Perm::Sudo, [](const Message& m, const Strings& L) {
        Message sent = reply(m, L["restarting"]);
        for (const char* d : {"cache", "downloads"}) { std::error_code ec; std::filesystem::remove_all(d, ec); }
        tg::client().edit_text(m.chat.id, sent.id, L["restarted"]);
        request_stop(true);
    });
}

}  // namespace bot
