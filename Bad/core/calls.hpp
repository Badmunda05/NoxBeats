// Port of bad/core/calls.py (TgCall). Playback orchestration on top of voice::Backend.
#pragma once
#include <map>
#include <memory>
#include <mutex>
#include "types.hpp"

namespace bot {

// Per-chat recursive lock: stream-end events, commands and button presses for the same chat are serialised.
std::shared_ptr<std::recursive_mutex> chat_lock(i64 chat);

class TgCall {
public:
    void boot();                                   // wire backend events
    void pause(i64 chat);
    void resume(i64 chat);
    void stop(i64 chat);
    // `mid` is the status message that will be turned into the "Started streaming" card.
    void play_media(i64 chat, i64 mid, const TrackPtr& media, int seek_time = 0);
    void replay(i64 chat);
    void play_next(i64 chat);
    double ping();
};

TgCall& bad();

}  // namespace bot
