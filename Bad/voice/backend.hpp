// Voice-chat abstraction. Implement with ntgcalls (Bad/voice/ntgcalls_backend.cpp in your tree).
// Replaces py-tgcalls.
#pragma once
#include <functional>
#include <stdexcept>
#include <string>
#include "../core/types.hpp"

namespace bot::voice {

enum class Err { NoActiveGroupCall, NoAudioSource, NotInCall, ConnectionNotFound, TelegramServer, RTMPUnsupported, FileNotFound, Timeout, Other };

struct Error : std::runtime_error {
    Err kind;
    Error(Err k, const std::string& w) : std::runtime_error(w), kind(k) {}
};

class Backend {
public:
    virtual ~Backend() = default;
    // idx is the 1-based assistant number.
    // seek > 1 => start from that second (ffmpeg -ss).
    virtual void play(int idx, i64 chat, const std::string& path, bool video, int seek) = 0;
    virtual void pause(int idx, i64 chat) = 0;
    virtual void resume(int idx, i64 chat) = 0;
    virtual void leave(int idx, i64 chat) = 0;
    virtual int participants(int idx, i64 chat) = 0;
    virtual double ping() = 0;   // ms, averaged over assistants

    // Events: the backend calls these from its own thread.
    std::function<void(i64 chat)> on_stream_ended;   // audio stream finished
    std::function<void(i64 chat)> on_chat_closed;    // kicked / left / voice chat closed
};

Backend& backend();
void set_backend(Backend* b);

}  // namespace bot::voice
