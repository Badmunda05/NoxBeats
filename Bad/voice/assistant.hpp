// Assistant accounts, fully in-process: MTProto (Bad/mtp) for joining chats / group calls and
// libntgcalls (media.hpp) for the audio stream. Replaces the old Go bridge processes.
#pragma once
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <vector>
#include <nlohmann/json.hpp>
#include "../mtp/client.hpp"
#include "backend.hpp"
#include "media.hpp"

namespace bot::voice {

struct AssistantOptions {
    std::vector<std::string> sessions;
    std::string session_type = "pyrogram";
    i64 api_id = 0;
    std::string tl_dir;            // directory with api.tl + mtproto.tl ("" = auto-detect)
    std::string dc_override;       // "ip:port" (tests only; also MTP_DC_OVERRIDE env)
};

using EngineFactory = std::function<std::unique_ptr<MediaEngine>()>;

class Assistant {
public:
    Assistant(int idx, const mtp::Schema& schema, mtp::SessionData s, std::uint32_t api_id, std::unique_ptr<MediaEngine> engine);
    ~Assistant();
    void start();                       // connect, fetch own profile; throws
    void stop();
    User user() const { return user_; }
    // paths: /play /pause /resume /leave /join /leave_chat /participants /dialogs /status
    nlohmann::json handle(const std::string& path, const nlohmann::json& body);

    std::function<void(i64 chat)> on_stream_end;
    std::function<void(i64 chat)> on_chat_closed;

private:
    struct Peer { bool channel = false; i64 id = 0; i64 hash = 0; };
    struct CallInfo { nlohmann::json input_call; std::int64_t ssrc = 0; };

    nlohmann::json rpc(const nlohmann::json& q, int timeout = 30) { return rpc_.call(q, timeout); }
    void learn(const nlohmann::json& v);                         // remember access hashes found anywhere in a result
    std::optional<Peer> find_peer(i64 chat);
    Peer resolve_peer(i64 chat);                                 // throws RpcError("CHANNEL_INVALID") if unknown
    nlohmann::json input_channel(const Peer& p) const;
    nlohmann::json get_input_call(const Peer& p);                // throws RpcError("GROUPCALL_INVALID") if none
    std::vector<i64> scan_dialogs();                             // fills the peer cache, returns bot-api style chat ids
    std::shared_ptr<std::mutex> chat_lock(i64 chat);

    nlohmann::json do_play(const nlohmann::json& b);
    nlohmann::json do_join(const nlohmann::json& b);
    nlohmann::json do_leave(i64 chat);
    nlohmann::json do_participants(i64 chat);

    int idx_;
    std::uint32_t api_id_;
    mtp::Client rpc_;
    std::unique_ptr<MediaEngine> engine_;
    User user_;
    std::mutex mu_;
    std::map<i64, Peer> peers_;             // key: bot-api chat id
    std::map<i64, i64> channel_hash_;       // channel id -> access hash
    std::map<i64, CallInfo> calls_;
    std::map<i64, std::shared_ptr<std::mutex>> locks_;
};

class AssistantManager {
public:
    explicit AssistantManager(AssistantOptions o, EngineFactory f = [] { return make_ntg_engine(); });
    ~AssistantManager() { stop(); }
    bool start(std::string* err);
    void stop();
    int count() const { return static_cast<int>(list_.size()); }
    User user(int idx) const { return list_.at(static_cast<size_t>(idx) - 1)->user(); }
    // Same contract the HTTP bridge had: returns {"ok":bool,"error":...,"detail":...}; throws only for a bad idx.
    nlohmann::json call(int idx, const std::string& path, const nlohmann::json& body, int timeout_sec = 60);
    nlohmann::json status(int idx) { return call(idx, "/status", nlohmann::json::object(), 10); }

    std::function<void(int idx, i64 chat)> on_stream_end;
    std::function<void(int idx, i64 chat)> on_chat_closed;

private:
    AssistantOptions opt_;
    EngineFactory factory_;
    mtp::Schema schema_;
    std::vector<std::unique_ptr<Assistant>> list_;
};

class AssistantBackend : public Backend {
public:
    explicit AssistantBackend(AssistantManager& m);
    void play(int idx, i64 chat, const std::string& path, bool video, int seek) override;
    void pause(int idx, i64 chat) override;
    void resume(int idx, i64 chat) override;
    void leave(int idx, i64 chat) override;
    int participants(int idx, i64 chat) override;
    double ping() override;
private:
    AssistantManager& m_;
};

}  // namespace bot::voice
