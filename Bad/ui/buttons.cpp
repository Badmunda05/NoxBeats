#include "buttons.hpp"
#include "../core/config.hpp"
#include "../tg/client.hpp"

namespace bot::buttons {
static Button cb(const std::string& t, const std::string& d) { return Button{t, d, "", ""}; }
static Button url(const std::string& t, const std::string& u) { return Button{t, "", u, ""}; }

Keyboard cancel_dl(const std::string& text) { return {{cb(text, "cancel_dl")}}; }

Keyboard controls(i64 chat, const std::string& status, const std::string& timer, bool remove) {
    std::string c = std::to_string(chat);
    Keyboard kb;
    if (!status.empty()) kb.push_back({cb(status, "controls status " + c)});
    else if (!timer.empty()) kb.push_back({cb(timer, "controls status " + c)});
    if (!remove)
        kb.push_back({cb("▷", "controls resume " + c), cb("II", "controls pause " + c),
                      cb("⥁", "controls replay " + c), cb("‣‣I", "controls skip " + c),
                      cb("▢", "controls stop " + c)});
    return kb;
}

Keyboard help_markup(const Strings& L, bool back) {
    if (back) return {{cb(L["back"], "help back"), cb(L["home"], "help home"), cb(L["close"], "help close")}};
    const char* cbs[] = {"admins", "auth", "blist", "lang", "ping", "play", "queue", "stats", "sudo"};
    Keyboard rows;
    std::vector<Button> row;
    for (int i = 0; i < 9; ++i) {
        row.push_back(cb(L[std::string("help_") + std::to_string(i)], std::string("help ") + cbs[i]));
        if (row.size() == 3) { rows.push_back(row); row.clear(); }
    }
    if (!row.empty()) rows.push_back(row);
    rows.push_back({cb(L["home"], "help home"), cb(L["close"], "help close")});
    return rows;
}

Keyboard lang_markup(const std::string& current) {
    Keyboard rows;
    std::vector<Button> row;
    for (const auto& [code, name] : lang().available()) {
        row.push_back(cb(name + " (" + code + ") " + (code == current ? "✔️" : ""), "lang_change " + code));
        if (row.size() == 2) { rows.push_back(row); row.clear(); }
    }
    if (!row.empty()) rows.push_back(row);
    return rows;
}

Keyboard ping_markup(const std::string& text) { return {{url(text, config().SUPPORT_CHAT)}}; }

Keyboard play_queued(i64 chat, const std::string& id, const std::string& text) {
    return {{cb(text, "controls force " + std::to_string(chat) + " " + id)}};
}

Keyboard queue_markup(i64 chat, const std::string& text, bool playing) {
    return {{cb(text, std::string("controls ") + (playing ? "pause" : "resume") + " " + std::to_string(chat) + " q")}};
}

Keyboard settings_markup(const Strings& L, bool admin_only, bool cmd_delete, const std::string& language) {
    auto onoff = [](bool b) { return std::string(b ? "True" : "False"); };   // shown as True / False
    auto codes = lang().codes();
    return {
        {cb(L["play_mode"] + " ➜", "settings"), cb(onoff(admin_only), "settings play")},
        {cb(L["cmd_delete"] + " ➜", "settings"), cb(onoff(cmd_delete), "settings delete")},
        {cb(L["language"] + " ➜", "settings"), cb(codes.count(language) ? codes[language] : language, "language")},
    };
}

Keyboard start_key(const Strings& L, bool is_private) {
    Keyboard rows = {
        {url(L["add_me"], "https://t.me/" + tg::client().me().username + "?startgroup=true")},
        {cb(L["help"], "help")},
        {url(L["support"], config().SUPPORT_CHAT), url(L["channel"], config().SUPPORT_CHANNEL)},
    };
    if (is_private) rows.push_back({url(L["source"], config().SOURCE_URL)});
    else rows.push_back({cb(L["language"], "language")});
    return rows;
}

Keyboard yt_key(const std::string& link) {
    return {{Button{"❐", "", "", link}, url("Youtube", link)}};
}

}  // namespace bot::buttons
