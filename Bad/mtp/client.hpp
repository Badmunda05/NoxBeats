// MTProto 2.0 client over the "intermediate" TCP transport, using an existing auth key (string session).
// One socket, one reader thread; requests are multiplexed by msg_id.
#pragma once
#include <atomic>
#include <condition_variable>
#include <functional>
#include <future>
#include <map>
#include <mutex>
#include <thread>
#include "session.hpp"
#include "tl.hpp"

namespace bot::mtp {

struct RpcError : std::runtime_error {
    int code;
    std::string text;   // e.g. "USER_ALREADY_PARTICIPANT", "FLOOD_WAIT_12"
    RpcError(int c, std::string t) : std::runtime_error("[" + std::to_string(c) + "] " + t), code(c), text(std::move(t)) {}
    int arg() const;    // numeric suffix (FLOOD_WAIT_12 -> 12) or 0
};

struct TransportError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

class Client {
public:
    Client(const Schema& schema, SessionData session, std::uint32_t api_id);
    ~Client();

    void start();    // connect + invokeWithLayer(initConnection(help.getConfig)); throws TransportError / RpcError
    void stop();
    bool connected() const { return connected_; }

    // Send a request ({"_": "users.getUsers", ...}) and wait for the typed result.
    // Reconnects (new MTProto session) once on transport failure.
    json call(const json& query, int timeout_sec = 30);

    std::function<void(const json& updates)> on_update;   // optional, called from the reader thread

private:
    struct Pending {
        Bytes body;
        std::string result_type;
        std::promise<json> done;
        bool resendable = true;
    };
    using PendingPtr = std::shared_ptr<Pending>;

    void connect_socket();
    void init_session();
    void reader_loop();
    void keepalive_loop();
    void close_socket();
    void fail_all(const std::string& why);
    std::int64_t next_msg_id();                        // caller holds send_mu_
    void send_body(const Bytes& body, std::int64_t& msg_id, bool content);   // locks send_mu_; assigns msg_id
    void send_ack(const std::vector<std::int64_t>& ids);
    void handle_packet(const Bytes& plain);
    void process(const std::uint8_t* d, std::size_t n, std::int64_t msg_id, std::int32_t seq, std::vector<std::int64_t>& acks);
    void complete(std::int64_t req_id, const std::uint8_t* d, std::size_t n);
    void resend(std::int64_t old_id);
    json call_once(const json& query, int timeout_sec);

    const Schema& schema_;
    SessionData sess_;
    std::uint32_t api_id_;
    Bytes key_id_;

    std::mutex send_mu_;      // serializes socket writes, msg_id and seq_no assignment
    std::mutex mu_;           // pending_ and state
    std::mutex reconnect_mu_;
    std::map<std::int64_t, PendingPtr> pending_;
    int fd_ = -1;
    std::atomic<bool> connected_{false};
    std::atomic<bool> stopping_{false};
    std::int64_t session_id_ = 0, salt_ = 0, last_msg_id_ = 0;
    std::int32_t seq_ = 0;
    std::atomic<std::int64_t> time_offset_us_{0};
    std::atomic<std::int64_t> last_rx_ms_{0};
    std::thread reader_, keepalive_;
    std::mutex ka_mu_;
    std::condition_variable ka_cv_;
};

}  // namespace bot::mtp
