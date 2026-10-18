// libntgcalls (C API) engine. Compiled only with -DWITH_NTGCALLS=ON.
#include "media.hpp"
#include <condition_variable>
#include <cstdlib>
#include <map>
#include <mutex>
#include "../core/log.hpp"

#ifdef WITH_NTGCALLS
#include "ntgcalls.h"

namespace bot::voice {
namespace {

struct FutState {
    std::mutex mu;
    std::condition_variable cv;
    bool ready = false;
    int code = 0;
    char* msg = nullptr;
};
using FutPtr = std::shared_ptr<FutState>;

void on_done(void* ud) {
    auto* sp = static_cast<FutPtr*>(ud);
    {
        std::lock_guard<std::mutex> lk((*sp)->mu);
        (*sp)->ready = true;
    }
    (*sp)->cv.notify_all();
    delete sp;   // drop the reference owned by the callback
}

// Runs `start(fut)` and waits for completion; throws MediaError on failure.
template <class F>
void run(const char* what, F start, int timeout_sec = 60) {
    auto st = std::make_shared<FutState>();
    auto* cb = new FutPtr(st);
    ntg_async_struct f{};
    f.userData = cb;
    f.promise = on_done;
    f.errorCode = &st->code;
    f.errorMessage = &st->msg;
    int rc = start(f);
    std::unique_lock<std::mutex> lk(st->mu);
    if (!st->cv.wait_for(lk, std::chrono::seconds(timeout_sec), [&] { return st->ready; }))
        throw MediaError(-1, std::string("ntgcalls ") + what + " timed out");
    std::string m;
    if (st->msg) { m = st->msg; std::free(st->msg); st->msg = nullptr; }
    int code = st->code < 0 ? st->code : (rc < 0 ? rc : 0);
    if (code < 0) throw MediaError(code, m.empty() ? std::string("ntgcalls ") + what + " failed (" + std::to_string(code) + ")" : m);
}

class NtgEngine : public MediaEngine {
public:
    NtgEngine() {
        h_ = ntg_init();
        if (!h_) throw MediaError(-1, "ntg_init failed");
        ntg_on_stream_end(h_, &NtgEngine::stream_end_thunk, this);
        ntg_on_connection_change(h_, &NtgEngine::conn_thunk, this);
    }
    ~NtgEngine() override { if (h_) ntg_destroy(h_); }

    std::string create(i64 chat) override {
        char* buf = nullptr;
        run("create", [&](ntg_async_struct f) { return ntg_create(h_, chat, &buf, f); });
        std::string out = buf ? buf : "";
        std::free(buf);
        if (out.empty()) throw MediaError(-1, "ntg_create returned no params");
        return out;
    }
    void set_source(i64 chat, const std::string& cmd) override {
        ntg_audio_description_struct a{};
        a.mediaSource = NTG_SHELL;
        a.input = const_cast<char*>(cmd.c_str());
        a.sampleRate = 96000;
        a.channelCount = 2;
        a.keepOpen = false;
        ntg_media_description_struct d{};
        d.microphone = &a;
        run("set_stream_sources", [&](ntg_async_struct f) { return ntg_set_stream_sources(h_, chat, NTG_STREAM_CAPTURE, d, f); });
    }
    void connect(i64 chat, const std::string& params) override {
        {
            std::lock_guard<std::mutex> lk(mu_);
            state_[chat] = -1;   // unknown/connecting
        }
        run("connect", [&](ntg_async_struct f) { return ntg_connect(h_, chat, const_cast<char*>(params.c_str()), false, f); });
        std::unique_lock<std::mutex> lk(mu_);
        bool ok = cv_.wait_for(lk, std::chrono::seconds(20), [&] { int s = state_[chat]; return s == NTG_STATE_CONNECTED || s == NTG_STATE_FAILED || s == NTG_STATE_TIMEOUT || s == NTG_STATE_CLOSED; });
        int s = state_[chat];
        if (!ok) throw MediaError(-1, "voice connection timeout");
        if (s != NTG_STATE_CONNECTED) throw MediaError(-302, "voice connection failed");
    }
    void pause(i64 chat) override { run("pause", [&](ntg_async_struct f) { return ntg_pause(h_, chat, f); }); }
    void resume(i64 chat) override { run("resume", [&](ntg_async_struct f) { return ntg_resume(h_, chat, f); }); }
    void stop(i64 chat) override {
        { std::lock_guard<std::mutex> lk(mu_); state_.erase(chat); }
        run("stop", [&](ntg_async_struct f) { return ntg_stop(h_, chat, f); });
    }
    i64 time(i64 chat) override {
        int64_t t = 0;
        run("time", [&](ntg_async_struct f) { return ntg_time(h_, chat, NTG_STREAM_CAPTURE, &t, f); });
        return t;
    }

private:
    static void stream_end_thunk(uintptr_t, int64_t chat, ntg_stream_type_enum type, ntg_stream_device_enum, void* ud) {
        if (type != NTG_STREAM_AUDIO) return;
        auto* self = static_cast<NtgEngine*>(ud);
        if (self->on_stream_end) self->on_stream_end(chat);
    }
    static void conn_thunk(uintptr_t, int64_t chat, ntg_network_info_struct info, void* ud) {
        auto* self = static_cast<NtgEngine*>(ud);
        bool was_connected;
        {
            std::lock_guard<std::mutex> lk(self->mu_);
            auto it = self->state_.find(chat);
            was_connected = it != self->state_.end() && it->second == NTG_STATE_CONNECTED;
            if (it != self->state_.end()) it->second = info.state;
        }
        self->cv_.notify_all();
        if (was_connected && (info.state == NTG_STATE_FAILED || info.state == NTG_STATE_CLOSED || info.state == NTG_STATE_TIMEOUT) && self->on_connection_lost)
            self->on_connection_lost(chat);
    }
    uintptr_t h_ = 0;
    std::mutex mu_;
    std::condition_variable cv_;
    std::map<i64, int> state_;
};
}  // namespace

std::unique_ptr<MediaEngine> make_ntg_engine() { return std::make_unique<NtgEngine>(); }
}  // namespace bot::voice

#else
namespace bot::voice {
std::unique_ptr<MediaEngine> make_ntg_engine() {
    throw MediaError(-1, "this build has no libntgcalls (configure with -DWITH_NTGCALLS=ON)");
}
}  // namespace bot::voice
#endif
