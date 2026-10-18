#include "util.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

namespace bot {

std::string User::mention() const {
    return "<a href=\"tg://user?id=" + std::to_string(id) + "\">" +
           util::html_escape(first_name.empty() ? std::to_string(id) : first_name) + "</a>";
}

namespace util {

std::string format_eta(long s) {
    char b[64];
    if (s < 60) { std::snprintf(b, sizeof b, "%lds", s); }
    else if (s < 3600) { std::snprintf(b, sizeof b, "%ld:%02ld min", s / 60, s % 60); }
    else { std::snprintf(b, sizeof b, "%ld:%02ld:%02ld h", s / 3600, (s % 3600) / 60, s % 60); }
    return b;
}

std::string format_size(double n) {
    char b[64];
    if (n >= 1024.0 * 1024 * 1024) std::snprintf(b, sizeof b, "%.2f GB", n / (1024.0 * 1024 * 1024));
    else if (n >= 1024.0 * 1024) std::snprintf(b, sizeof b, "%.2f MB", n / (1024.0 * 1024));
    else std::snprintf(b, sizeof b, "%.2f KB", n / 1024.0);
    return b;
}

int to_seconds(const std::string& t) {
    long total = 0;
    size_t i = 0;
    std::string s = trim(t);
    while (i <= s.size()) {
        size_t j = s.find(':', i);
        if (j == std::string::npos) j = s.size();
        std::string part = s.substr(i, j - i);
        if (!is_digits(part)) return 0;
        total = total * 60 + std::stol(part);
        i = j + 1;
        if (j == s.size()) break;
    }
    return static_cast<int>(total);
}

std::string mmss(long s) {
    if (s < 0) s = 0;
    char b[32];
    std::snprintf(b, sizeof b, "%02ld:%02ld", (s / 60) % 60, s % 60);
    return b;
}

std::string html_escape(const std::string& s) {
    std::string o;
    o.reserve(s.size());
    for (char c : s) {
        if (c == '&') o += "&amp;";
        else if (c == '<') o += "&lt;";
        else if (c == '>') o += "&gt;";
        else if (c == '"') o += "&quot;";
        else o += c;
    }
    return o;
}

std::string trim(const std::string& s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

std::vector<std::string> split_ws(const std::string& s) {
    std::vector<std::string> out;
    size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && std::isspace(static_cast<unsigned char>(s[i]))) ++i;
        size_t j = i;
        while (j < s.size() && !std::isspace(static_cast<unsigned char>(s[j]))) ++j;
        if (j > i) out.push_back(s.substr(i, j - i));
        i = j;
    }
    return out;
}

std::string join(const std::vector<std::string>& v, const std::string& sep, size_t from) {
    std::string o;
    for (size_t i = from; i < v.size(); ++i) {
        if (i > from) o += sep;
        o += v[i];
    }
    return o;
}

bool is_digits(const std::string& s) {
    return !s.empty() && std::all_of(s.begin(), s.end(), [](unsigned char c) { return std::isdigit(c); });
}
bool starts_with(const std::string& s, const std::string& p) { return s.rfind(p, 0) == 0; }
bool contains(const std::vector<std::string>& v, const std::string& x) {
    return std::find(v.begin(), v.end(), x) != v.end();
}
std::string lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return std::tolower(c); });
    return s;
}

std::string uptime_string(long s) {
    // ping.py: "Xdays, Hh:Mm:Ss" - days part is dropped when zero
    long sec = s % 60, min = (s / 60) % 60, hr = (s / 3600) % 24, days = s / 86400;
    std::string o;
    if (days) o = std::to_string(days) + "days, ";
    o += std::to_string(hr) + "h:" + std::to_string(min) + "m:" + std::to_string(sec) + "s";
    return o;
}

std::string truncate(const std::string& s, size_t n) {
    size_t cp = 0, i = 0;
    while (i < s.size() && cp < n) {
        unsigned char c = s[i];
        i += (c < 0x80) ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : 4;
        ++cp;
    }
    return s.substr(0, std::min(i, s.size()));
}

// Telegram entity offsets are in UTF-16 code units.
std::optional<std::string> utf16_substr(const std::string& s, int off, int len) {
    int u16 = 0;
    size_t i = 0, start = std::string::npos, end = std::string::npos;
    while (i <= s.size()) {
        if (u16 == off && start == std::string::npos) start = i;
        if (u16 == off + len) { end = i; break; }
        if (i == s.size()) break;
        unsigned char c = s[i];
        int bytes = (c < 0x80) ? 1 : (c >> 5) == 6 ? 2 : (c >> 4) == 14 ? 3 : 4;
        u16 += (bytes == 4) ? 2 : 1;
        i += bytes;
    }
    if (start == std::string::npos || end == std::string::npos) return std::nullopt;
    return s.substr(start, end - start);
}

std::string url_of(const Message& m1) {
    std::string link;
    std::vector<const Message*> msgs{&m1};
    if (m1.reply_to) msgs.push_back(m1.reply_to.get());
    for (const Message* m : msgs) {
        for (const Entity& e : m->entities) {
            if (e.type == Entity::Type::TextLink) { link = e.url; break; }
            if (e.type == Entity::Type::Url) {
                if (m->text.empty()) continue;
                if (auto sub = utf16_substr(m->text, e.offset, e.length)) { link = *sub; break; }
            }
        }
    }
    if (link.empty()) return "";
    size_t p = link.find("&si");
    if (p != std::string::npos) link.resize(p);
    p = link.find("?si");
    if (p != std::string::npos) link.resize(p);
    return link;
}

ExecResult exec(const std::vector<std::string>& argv, int timeout_sec) {
    ExecResult r;
    if (argv.empty()) return r;
    int pfd[2], efd[2];
    if (pipe(pfd) != 0) return r;
    if (pipe(efd) != 0) { close(pfd[0]); close(pfd[1]); return r; }
    pid_t pid = fork();
    if (pid < 0) { close(pfd[0]); close(pfd[1]); close(efd[0]); close(efd[1]); return r; }
    if (pid == 0) {
        dup2(pfd[1], STDOUT_FILENO);
        dup2(efd[1], STDERR_FILENO);
        close(pfd[0]); close(pfd[1]); close(efd[0]); close(efd[1]);
        std::vector<char*> args;
        for (const auto& a : argv) args.push_back(const_cast<char*>(a.c_str()));
        args.push_back(nullptr);
        execvp(args[0], args.data());
        _exit(127);
    }
    close(pfd[1]); close(efd[1]);
    auto t0 = std::chrono::steady_clock::now();
    char buf[4096];
    bool timed_out = false;
    bool out_open = true, err_open = true;
    while (out_open || err_open) {
        int wait_ms = 500;
        if (timeout_sec > 0) {
            auto el = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - t0).count();
            if (el >= timeout_sec * 1000L) { timed_out = true; break; }
        }
        struct pollfd p[2] = {{out_open ? pfd[0] : -1, POLLIN, 0}, {err_open ? efd[0] : -1, POLLIN, 0}};
        int pr = poll(p, 2, wait_ms);
        if (pr > 0) {
            if (out_open && (p[0].revents & (POLLIN | POLLHUP | POLLERR))) {
                ssize_t n = read(pfd[0], buf, sizeof buf);
                if (n <= 0) out_open = false; else r.out.append(buf, static_cast<size_t>(n));
            }
            if (err_open && (p[1].revents & (POLLIN | POLLHUP | POLLERR))) {
                ssize_t n = read(efd[0], buf, sizeof buf);
                if (n <= 0) err_open = false;
                else if (r.err.size() < 8192) r.err.append(buf, static_cast<size_t>(n));
            }
        }
    }
    close(pfd[0]); close(efd[0]);
    if (timed_out) kill(pid, SIGKILL);
    int st = 0;
    waitpid(pid, &st, 0);
    r.code = timed_out ? -2 : (WIFEXITED(st) ? WEXITSTATUS(st) : -1);
    return r;
}

}  // namespace util
}  // namespace bot
