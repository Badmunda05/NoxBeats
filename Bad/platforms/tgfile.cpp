#include "tgfile.hpp"
#include <chrono>
#include <filesystem>
#include "../core/config.hpp"
#include "../core/util.hpp"
#include "../tg/client.hpp"
#include "../ui/buttons.hpp"

namespace bot {

TelegramFiles& tgfiles() {
    static TelegramFiles t;
    return t;
}

bool TelegramFiles::cancel(i64, i64 msg_id) {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = cancels_.find(msg_id);
    if (it == cancels_.end()) return false;
    it->second->store(true);
    return true;
}

std::optional<TrackPtr> TelegramFiles::download(const Message& m, i64 chat, i64 sent_id, const Strings& L) {
    using clock = std::chrono::steady_clock;
    auto& c = tg::client();
    auto cancel = std::make_shared<std::atomic<bool>>(false);
    { std::lock_guard<std::mutex> lk(mu_); cancels_[sent_id] = cancel; }
    auto cleanup = [&](const std::string& fid) {
        std::lock_guard<std::mutex> lk(mu_);
        cancels_.erase(sent_id);
        active_.erase(fid);
    };

    const Message& src = m;
    std::string file_id = src.media_unique_id;
    // the id becomes a file name: keep it to a safe alphabet
    if (file_id.empty() || file_id.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789_-") != std::string::npos) {
        cleanup(""); c.edit_text(chat, sent_id, L.f("play_unsupported", {})); return std::nullopt;
    }
    std::string ext = src.media_ext;
    if (ext.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789") != std::string::npos) ext = "";
    bool video = util::starts_with(src.media_mime, "video/");
    auto start = clock::now();

    if (src.media_duration > config().DURATION_LIMIT) {
        c.edit_text(chat, sent_id, L.f("play_duration_limit", {config().DURATION_LIMIT / 60}));
        cleanup(file_id); return std::nullopt;
    }
    if (src.media_size > c.max_download()) {
        c.edit_text(chat, sent_id, L["dl_limit"] + "\n\nLimit: " + util::format_size(static_cast<double>(c.max_download())));
        cleanup(file_id); return std::nullopt;
    }

    std::string path = "downloads/" + file_id + (ext.empty() ? "" : "." + ext);
    std::filesystem::create_directories("downloads");
    try {
        if (!std::filesystem::exists(path)) {
            {
                std::lock_guard<std::mutex> lk(mu_);
                if (!active_.insert(file_id).second) {
                    c.edit_text(chat, sent_id, L["dl_active"]);
                    cancels_.erase(sent_id);
                    return std::nullopt;
                }
            }
            auto last_edit = clock::now() - std::chrono::seconds(10);
            auto progress = [&](tg::Progress p) {
                auto now = clock::now();
                if (cancel->load() || now - last_edit < std::chrono::seconds(5) || p.total <= 0) return;
                last_edit = now;
                double el = std::chrono::duration<double>(now - start).count();
                if (el < 1e-6) el = 1e-6;
                double speed = p.current / el;
                long eta = speed > 0 ? static_cast<long>((p.total - p.current) / speed) : 0;
                try {
                    c.edit_text(chat, sent_id,
                                L.f("dl_progress", {util::format_size(p.current), util::format_size(p.total),
                                                    p.current * 100.0 / p.total, util::format_size(speed), util::format_eta(eta)}),
                                buttons::cancel_dl(L["cancel"]));
                } catch (const tg::Error&) {}
            };
            bool ok = c.download_media(src, path, progress, *cancel);
            if (!ok || cancel->load()) { cleanup(file_id); std::filesystem::remove(path); return std::nullopt; }
            double took = std::chrono::duration<double>(clock::now() - start).count();
            char b[32]; std::snprintf(b, sizeof b, "%.2f", took);
            c.edit_text(chat, sent_id, L.f("dl_complete", {std::string(b)}));
        }
    } catch (...) { cleanup(file_id); throw; }
    cleanup(file_id);

    auto t = std::make_shared<Track>();
    t->id = file_id;
    t->duration = util::mmss(src.media_duration);
    t->duration_sec = src.media_duration;
    t->file_path = path;
    t->message_id = sent_id;
    t->url = src.link;
    t->title = util::truncate(src.media_title.empty() ? "Telegram File" : src.media_title, 25);
    t->video = video;
    t->is_youtube = false;
    return t;
}

TrackPtr TelegramFiles::process_m3u8(const std::string& url, i64 msg_id, bool video) {
    auto t = std::make_shared<Track>();
    t->id = std::to_string(msg_id);
    t->file_path = url;
    t->message_id = msg_id;
    t->url = url;
    t->title = "M3U8 Stream";
    t->video = video;
    t->is_youtube = false;
    return t;
}

}  // namespace bot
