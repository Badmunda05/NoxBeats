// Media engine = the WebRTC side of a voice chat (ntgcalls). Abstract so tests can use a fake.
#pragma once
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include "../core/types.hpp"

namespace bot::voice {

struct MediaError : std::runtime_error {
    int code;   // ntgcalls error code (<0) or 0
    MediaError(int c, const std::string& w) : std::runtime_error(w), code(c) {}
};

class MediaEngine {
public:
    virtual ~MediaEngine() = default;
    // Prepare the WebRTC side of a call; returns the JSON "params" for phone.joinGroupCall.
    virtual std::string create(i64 chat) = 0;
    // Attach the sources: a shell command writing raw s16le stereo PCM to stdout, and optionally
    // (video_cmd non-empty) a second one writing raw yuv420p frames of kVideoWidth x kVideoHeight at kVideoFps.
    virtual void set_source(i64 chat, const std::string& audio_cmd, const std::string& video_cmd) = 0;
    // Feed Telegram's answer (updateGroupCallConnection.params.data); blocks until connected or throws.
    virtual void connect(i64 chat, const std::string& remote_params) = 0;
    virtual void mute(i64 chat) = 0;
    virtual void unmute(i64 chat) = 0;
    virtual void pause(i64 chat) = 0;
    virtual void resume(i64 chat) = 0;
    virtual void stop(i64 chat) = 0;
    virtual i64 time(i64 chat) = 0;   // seconds played

    std::function<void(i64 chat)> on_stream_end;
    std::function<void(i64 chat)> on_connection_lost;   // failed / closed after being connected
};

// POSIX single-quote a string so it is one inert shell word.
std::string shell_quote(const std::string& s);
// `ffmpeg ... -i '<src>' -f s16le -ac 2 -ar 96000 pipe:1`. Throws std::invalid_argument for unusable input.
std::string build_ffmpeg_cmd(const std::string& src, int seek_sec, double speed = 1.0, int bass = 0);   // bass 0..100 (0 = off)

constexpr int kVideoWidth = 1280, kVideoHeight = 720, kVideoFps = 30;
// `ffmpeg ... -an -f rawvideo -pix_fmt yuv420p -s 1280x720 -r 30 pipe:1` (letterboxed, aspect ratio kept).
std::string build_ffmpeg_video_cmd(const std::string& src, int seek_sec, double speed = 1.0);

// Real engine (libntgcalls). Throws MediaError when the binary was built without ntgcalls.
std::unique_ptr<MediaEngine> make_ntg_engine();

}  // namespace bot::voice
