// RTMP out: /setrtmp (DM) stores a chat's RTMP url + key, /stream pushes a video to it with ffmpeg,
// /streamstop and /streamstatus. ffmpeg is spawned with fork/exec (no shell); one child per chat.
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <map>
#include <mutex>
#include <thread>
#include "common.hpp"
#include "../core/config.hpp"
#include "../core/log.hpp"
#include "../core/util.hpp"
#include "../db/db.hpp"
#include "../platforms/yt.hpp"
#include "../tg/client.hpp"

namespace bot {
namespace {

struct Stream {
    pid_t pid = 0;
    std::string title, log_path;
    i64 started_ms = 0;
    bool stopping = false;
};
std::mutex g_mu;
std::map<i64, Stream> g_streams;
std::atomic<bool> g_watcher{false};

std::string tail_of(const std::string& path, size_t lines) {
    std::ifstream f(path);
    std::vector<std::string> all;
    std::string l;
    while (std::getline(f, l)) { if (!l.empty()) all.push_back(l); if (all.size() > 200) all.erase(all.begin()); }
    std::string out;
    for (size_t i = all.size() > lines ? all.size() - lines : 0; i < all.size(); ++i) out += all[i] + "\n";
    return out;
}

// Never show the stream key: scrub anything that looks like the target url
std::string scrub(std::string s, const std::string& secret) {
    if (secret.empty()) return s;
    size_t p = 0;
    while ((p = s.find(secret, p)) != std::string::npos) { s.replace(p, secret.size(), "***"); p += 3; }
    return s;
}

std::string target_of(i64 chat) {
    auto [url, key] = db().get_rtmp(chat);
    if (url.empty()) return "";
    if (key.empty()) return url;
    return url.back() == '/' ? url + key : url + "/" + key;
}

pid_t spawn(const std::vector<std::string>& argv, const std::string& log_path) {
    pid_t pid = fork();
    if (pid != 0) return pid;
    setsid();
    int in = open("/dev/null", O_RDONLY);
    int lg = open(log_path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (in >= 0) dup2(in, 0);
    if (lg >= 0) { dup2(lg, 1); dup2(lg, 2); }
    std::vector<char*> a;
    for (const auto& s : argv) a.push_back(const_cast<char*>(s.c_str()));
    a.push_back(nullptr);
    execvp(a[0], a.data());
    _exit(127);
}

void kill_stream(pid_t pid) {
    if (pid <= 0) return;
    kill(-pid, SIGTERM);
    for (int i = 0; i < 20; ++i) {
        int st;
        if (waitpid(pid, &st, WNOHANG) != 0) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    kill(-pid, SIGKILL);
    waitpid(pid, nullptr, 0);
}

void start_watcher() {
    if (g_watcher.exchange(true)) return;
    std::thread([] {
        for (;;) {
            std::this_thread::sleep_for(std::chrono::seconds(3));
            struct Done { i64 chat; std::string title, log, secret; int code; };
            std::vector<Done> done;
            {
                std::lock_guard<std::mutex> lk(g_mu);
                for (auto it = g_streams.begin(); it != g_streams.end();) {
                    int st = 0;
                    pid_t r = waitpid(it->second.pid, &st, WNOHANG);
                    if (r == 0) { ++it; continue; }
                    if (!it->second.stopping)
                        done.push_back({it->first, it->second.title, it->second.log_path, target_of(it->first),
                                        WIFEXITED(st) ? WEXITSTATUS(st) : 128 + (WIFSIGNALED(st) ? WTERMSIG(st) : 0)});
                    it = g_streams.erase(it);
                }
            }
            for (auto& d : done) {
                try {
                    std::string t = scrub(tail_of(d.log, 4), d.secret);
                    tg::client().send_message(d.chat, "<b>Stream ended</b> (<code>" + util::html_escape(d.title) + "</code>), exit code " +
                                              std::to_string(d.code) + (d.code ? "\n<pre>" + util::html_escape(t) + "</pre>" : ""), {}, 0);
                } catch (...) {}
            }
        }
    }).detach();
}

bool valid_rtmp(const std::string& u) {
    std::string l = util::lower(u);
    return (l.rfind("rtmp://", 0) == 0 || l.rfind("rtmps://", 0) == 0) && l.find(' ') == std::string::npos && u.size() < 400;
}

}  // namespace

void register_rtmp() {
    register_module("rtmp");
    auto& c = tg::client;

    // /setrtmp <chat_id> <rtmp://host/app> [key]   -- or the whole url with the key at the end
    on_command({"setrtmp"}, Scope::Private, Perm::Everyone, [](const Message& m, const Strings& L) {
        if (m.command.size() < 3) return reply(m, L["rtmp_usage"]), void();
        i64 target = 0;
        try { target = std::stoll(m.command[1]); } catch (...) { return reply(m, L["rtmp_usage"]), void(); }
        if (!is_sudo(m.from->id) && !is_admin(target, m.from->id)) return reply(m, L["rtmp_not_admin"]), void();
        std::string url = m.command[2], key = m.command.size() > 3 ? m.command[3] : "";
        if (url == "off" || url == "remove") {
            db().set_rtmp(target, "", "");
            return reply(m, L["rtmp_removed"]), void();
        }
        if (!valid_rtmp(url)) return reply(m, L["rtmp_invalid"]), void();
        db().set_rtmp(target, url, key);
        reply(m, L["rtmp_saved"]);
    });

    on_command({"stream"}, Scope::Group, Perm::Everyone, [](const Message& m, const Strings& L) {
        i64 chat = m.chat.id;
        if (!can_manage_vc(m, L)) return;
        std::string target = target_of(chat);
        if (target.empty()) return reply(m, L["rtmp_not_set"]), void();
        std::string query = util::join(m.command, " ", 1);
        std::string url = util::url_of(m);
        if (query.empty() && url.empty()) return reply(m, L["stream_usage"]), void();
        {
            std::lock_guard<std::mutex> lk(g_mu);
            if (g_streams.count(chat)) return reply(m, L["stream_running"]), void();
        }
        Message sent = c().send_message(chat, L["play_searching"], {}, m.id);
        auto t = yt().search(url.empty() ? query : url, sent.id, true);
        if (!t) return c().edit_text(chat, sent.id, L.f("play_not_found", {config().SUPPORT_CHAT})), void();
        if (t->duration_sec > config().DURATION_LIMIT * 6)
            return c().edit_text(chat, sent.id, L.f("play_duration_limit", {config().DURATION_LIMIT * 6 / 60})), void();
        c().edit_text(chat, sent.id, L["play_downloading"]);
        std::string file = yt().download(t->id, true);
        if (file.empty()) return c().edit_text(chat, sent.id, L.f("play_not_found", {config().SUPPORT_CHAT})), void();

        std::error_code ec;
        std::filesystem::create_directories("downloads", ec);
        std::string log_path = "downloads/stream_" + std::to_string(chat < 0 ? -chat : chat) + ".log";
        std::vector<std::string> argv = {"ffmpeg", "-hide_banner", "-loglevel", "warning", "-re", "-i", file,
            "-c:v", "libx264", "-preset", "veryfast", "-tune", "zerolatency", "-pix_fmt", "yuv420p",
            "-b:v", "2500k", "-maxrate", "2500k", "-bufsize", "5000k", "-g", "60", "-r", "30",
            "-c:a", "aac", "-b:a", "128k", "-ar", "44100", "-f", "flv", target};
        pid_t pid = spawn(argv, log_path);
        if (pid < 0) return c().edit_text(chat, sent.id, L["stream_failed"]), void();
        {
            std::lock_guard<std::mutex> lk(g_mu);
            g_streams[chat] = Stream{pid, t->title, log_path, Track::now_ms(), false};
        }
        start_watcher();
        c().edit_text(chat, sent.id, L.f("stream_started", {util::html_escape(t->title), m.from->mention()}));
    });

    on_command({"streamstop"}, Scope::Group, Perm::Everyone, [](const Message& m, const Strings& L) {
        if (!can_manage_vc(m, L)) return;
        pid_t pid = 0;
        {
            std::lock_guard<std::mutex> lk(g_mu);
            auto it = g_streams.find(m.chat.id);
            if (it == g_streams.end()) return reply(m, L["stream_none"]), void();
            it->second.stopping = true;
            pid = it->second.pid;
        }
        kill_stream(pid);
        { std::lock_guard<std::mutex> lk(g_mu); g_streams.erase(m.chat.id); }
        reply(m, L.f("stream_stopped", {m.from->mention()}));
    });

    on_command({"streamstatus"}, Scope::Group, Perm::Everyone, [](const Message& m, const Strings& L) {
        std::lock_guard<std::mutex> lk(g_mu);
        auto it = g_streams.find(m.chat.id);
        if (it == g_streams.end()) return reply(m, L["stream_none"]), void();
        int secs = static_cast<int>((Track::now_ms() - it->second.started_ms) / 1000);
        reply(m, L.f("stream_status", {util::html_escape(it->second.title), util::mmss(secs)}));
    });
}

}  // namespace bot
