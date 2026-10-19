// Download audio/video/document replies with a progress message.
#pragma once
#include <atomic>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include "../core/types.hpp"
#include "../i18n/lang.hpp"

namespace bot {

class TelegramFiles {
public:
    static bool get_media(const Message& m) { return m.has_media; }
    // `sent` is the status message that shows progress. nullopt => aborted (error already shown / cancelled).
    std::optional<TrackPtr> download(const Message& m, i64 chat, i64 sent_id, const Strings& L);
    // Handles the "cancel_dl" button. Returns true if a download was cancelled.
    bool cancel(i64 chat, i64 msg_id);
    TrackPtr process_m3u8(const std::string& url, i64 msg_id, bool video);
private:
    std::mutex mu_;
    std::set<std::string> active_;
    std::map<i64, std::shared_ptr<std::atomic<bool>>> cancels_;
};

TelegramFiles& tgfiles();

}  // namespace bot
