#include "client.hpp"
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <set>
#include "../core/log.hpp"
#include "crypto.hpp"

namespace bot::mtp {
namespace {
constexpr int kLayer = 222;
constexpr std::uint32_t kContainer = 0x73f1f8dc, kRpcResult = 0xf35c6d01, kRpcError = 0x2144ca19, kGzip = 0x3072cfa1,
                        kBadSalt = 0xedab447b, kBadMsg = 0xa7eff811, kNewSession = 0x9ec20908, kPong = 0x347773c5,
                        kMsgsAck = 0x62d6b459;

std::int64_t now_us() {
    return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
}
std::int64_t mono_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
}  // namespace

int RpcError::arg() const {
    size_t p = text.rfind('_');
    if (p == std::string::npos) return 0;
    try { return std::stoi(text.substr(p + 1)); } catch (...) { return 0; }
}

Client::Client(const Schema& schema, SessionData s, std::uint32_t api_id)
    : schema_(schema), sess_(std::move(s)), api_id_(api_id), key_id_(crypto::auth_key_id(sess_.auth_key)) {}

Client::~Client() { stop(); }

// ---------------------------------------------------------------- socket
void Client::connect_socket() {
    std::vector<std::string> ips{sess_.ip};
    ips.insert(ips.end(), sess_.fallback_ips.begin(), sess_.fallback_ips.end());
    std::string last_err = "no address";
    for (const auto& ip : ips) {
        int fd = ::socket(AF_INET, SOCK_STREAM, 0);
        if (fd < 0) { last_err = "socket(): " + std::string(strerror(errno)); continue; }
        sockaddr_in a{};
        a.sin_family = AF_INET;
        a.sin_port = htons(static_cast<uint16_t>(sess_.port));
        if (inet_pton(AF_INET, ip.c_str(), &a.sin_addr) != 1) { ::close(fd); last_err = "bad address " + ip; continue; }
        timeval tv{10, 0};
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
        if (::connect(fd, reinterpret_cast<sockaddr*>(&a), sizeof a) != 0) {
            last_err = ip + ": " + strerror(errno);
            ::close(fd);
            continue;
        }
        timeval rv{30, 0};
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &rv, sizeof rv);
        int one = 1;
        setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof one);
        setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &one, sizeof one);
        const std::uint8_t hello[4] = {0xee, 0xee, 0xee, 0xee};   // intermediate transport
        if (::send(fd, hello, 4, MSG_NOSIGNAL) != 4) { last_err = "handshake send failed"; ::close(fd); continue; }
        fd_ = fd;
        log::info("MTProto: connected to DC" + std::to_string(sess_.dc_id) + " " + ip + ":" + std::to_string(sess_.port));
        return;
    }
    throw TransportError("cannot connect to Telegram DC" + std::to_string(sess_.dc_id) + " (" + last_err + ")");
}

void Client::close_socket() {
    int fd;
    {
        std::lock_guard<std::mutex> lk(send_mu_);
        fd = fd_;
        fd_ = -1;
    }
    if (fd >= 0) { ::shutdown(fd, SHUT_RDWR); ::close(fd); }
}

void Client::fail_all(const std::string& why) {
    std::map<std::int64_t, PendingPtr> p;
    {
        std::lock_guard<std::mutex> lk(mu_);
        p.swap(pending_);
    }
    for (auto& [id, pp] : p) {
        try { pp->done.set_exception(std::make_exception_ptr(TransportError(why))); } catch (...) {}
    }
}

// ---------------------------------------------------------------- lifecycle
void Client::start() {
    std::lock_guard<std::mutex> rk(reconnect_mu_);
    if (connected_) return;
    stopping_ = false;
    if (reader_.joinable()) reader_.join();
    connect_socket();
    {
        std::lock_guard<std::mutex> lk(send_mu_);
        session_id_ = static_cast<std::int64_t>(crypto::random_u64());
        seq_ = 0;
        last_msg_id_ = 0;
        salt_ = 0;
    }
    connected_ = true;
    last_rx_ms_ = mono_ms();
    reader_ = std::thread([this] { reader_loop(); });
    try {
        init_session();
    } catch (...) {
        connected_ = false;
        close_socket();
        if (reader_.joinable()) reader_.join();
        throw;
    }
    if (!keepalive_.joinable()) keepalive_ = std::thread([this] { keepalive_loop(); });
}

void Client::init_session() {
    json q = {{"_", "invokeWithLayer"}, {"layer", kLayer},
              {"query", {{"_", "initConnection"}, {"api_id", api_id_}, {"device_model", "Linux"}, {"system_version", "Docker"},
                         {"app_version", "2.0"}, {"system_lang_code", "en"}, {"lang_pack", ""}, {"lang_code", "en"},
                         {"query", {{"_", "help.getConfig"}}}}}};
    call_once(q, 30);
}

void Client::stop() {
    stopping_ = true;
    connected_ = false;
    ka_cv_.notify_all();
    close_socket();
    if (reader_.joinable() && std::this_thread::get_id() != reader_.get_id()) reader_.join();
    if (keepalive_.joinable() && std::this_thread::get_id() != keepalive_.get_id()) keepalive_.join();
    fail_all("client stopped");
}

void Client::keepalive_loop() {
    while (!stopping_) {
        {
            std::unique_lock<std::mutex> lk(ka_mu_);
            ka_cv_.wait_for(lk, std::chrono::seconds(30), [this] { return stopping_.load(); });
        }
        if (stopping_) return;
        if (!connected_) continue;
        try {
            json ping = {{"_", "ping_delay_disconnect"}, {"ping_id", static_cast<std::int64_t>(crypto::random_u64() >> 1)}, {"disconnect_delay", 75}};
            Bytes body = schema_.serialize(ping);
            std::int64_t id = 0;
            send_body(body, id, true);
        } catch (const std::exception& e) {
            log::warn(std::string("MTProto keepalive failed: ") + e.what());
        }
        if (connected_ && mono_ms() - last_rx_ms_ > 150000) {
            log::warn("MTProto: connection silent for 150s, dropping it");
            close_socket();
        }
    }
}

// ---------------------------------------------------------------- sending
std::int64_t Client::next_msg_id() {
    std::int64_t us = now_us() + time_offset_us_;
    std::uint64_t sec = static_cast<std::uint64_t>(us / 1000000), frac = static_cast<std::uint64_t>(us % 1000000);
    std::int64_t id = static_cast<std::int64_t>((sec << 32) | (((frac << 32) / 1000000) & ~std::uint64_t{3}));
    if (id <= last_msg_id_) id = last_msg_id_ + 4;
    last_msg_id_ = id;
    return id;
}

void Client::send_body(const Bytes& body, std::int64_t& msg_id, bool content) {
    std::lock_guard<std::mutex> lk(send_mu_);
    if (fd_ < 0) throw TransportError("not connected");
    msg_id = next_msg_id();
    std::int32_t seqno = content ? seq_ * 2 + 1 : seq_ * 2;
    if (content) ++seq_;

    Bytes plain;
    put_i64(plain, salt_);
    put_i64(plain, session_id_);
    put_i64(plain, msg_id);
    put_i32(plain, seqno);
    put_i32(plain, static_cast<std::int32_t>(body.size()));
    plain.insert(plain.end(), body.begin(), body.end());
    std::size_t pad = 16 - (plain.size() % 16);
    if (pad < 12) pad += 16;
    std::size_t at = plain.size();
    plain.resize(at + pad);
    crypto::random_bytes(plain.data() + at, pad);

    Bytes pkt = crypto::encrypt_packet(sess_.auth_key, key_id_, plain, true);
    Bytes wire;
    put_i32(wire, static_cast<std::int32_t>(pkt.size()));
    wire.insert(wire.end(), pkt.begin(), pkt.end());
    std::size_t off = 0;
    while (off < wire.size()) {
        ssize_t n = ::send(fd_, wire.data() + off, wire.size() - off, MSG_NOSIGNAL);
        if (n <= 0) throw TransportError(std::string("send failed: ") + strerror(errno));
        off += static_cast<std::size_t>(n);
    }
}

void Client::send_ack(const std::vector<std::int64_t>& ids) {
    if (ids.empty()) return;
    json a = {{"_", "msgs_ack"}, {"msg_ids", ids}};
    std::int64_t id = 0;
    try { send_body(schema_.serialize(a), id, false); } catch (...) {}
}

// ---------------------------------------------------------------- calls
json Client::call_once(const json& query, int timeout_sec) {
    auto p = std::make_shared<Pending>();
    p->body = schema_.serialize(query);
    p->result_type = schema_.result_type(query);
    auto fut = p->done.get_future();
    std::int64_t id = 0;
    {
        // register under the id that send_body assigns: hold mu_ across the send so the reader cannot see a reply first
        std::lock_guard<std::mutex> lk(mu_);
        try {
            send_body(p->body, id, true);
        } catch (...) { throw; }
        pending_[id] = p;
    }
    if (fut.wait_for(std::chrono::seconds(timeout_sec)) != std::future_status::ready) {
        std::lock_guard<std::mutex> lk(mu_);
        for (auto it = pending_.begin(); it != pending_.end(); ++it)
            if (it->second == p) { pending_.erase(it); break; }
        throw TransportError("request timed out: " + query.value("_", std::string("?")));
    }
    return fut.get();
}

json Client::call(const json& query, int timeout_sec) {
    for (int attempt = 0;; ++attempt) {
        try {
            if (!connected_) start();
            return call_once(query, timeout_sec);
        } catch (const TransportError& e) {
            if (stopping_ || attempt >= 1) throw;
            log::warn(std::string("MTProto transport error (") + e.what() + "), reconnecting");
            connected_ = false;
            close_socket();
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
    }
}

// ---------------------------------------------------------------- receiving
void Client::reader_loop() {
    auto read_full = [this](int fd, std::uint8_t* buf, std::size_t n) -> bool {
        std::size_t got = 0;
        while (got < n) {
            ssize_t r = ::recv(fd, buf + got, n - got, 0);
            if (r > 0) { got += static_cast<std::size_t>(r); continue; }
            if (r < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                if (stopping_ || mono_ms() - last_rx_ms_ > 150000) return false;
                continue;
            }
            return false;
        }
        return true;
    };
    std::string why = "connection closed";
    int fd;
    { std::lock_guard<std::mutex> lk(send_mu_); fd = fd_; }
    while (!stopping_ && fd >= 0) {
        std::uint8_t lenb[4];
        if (!read_full(fd, lenb, 4)) break;
        std::uint32_t len = get_u32(lenb);
        if (len > (16u << 20) || len < 4) { why = "bad frame length"; break; }
        Bytes pkt(len);
        if (!read_full(fd, pkt.data(), len)) break;
        last_rx_ms_ = mono_ms();
        if (len == 4) {
            std::int32_t code = get_i32(pkt.data());
            why = "transport error " + std::to_string(code) + (code == -404 ? " (auth key unknown to this DC: SESSION is invalid or revoked)" : "");
            log::error("MTProto: " + why);
            break;
        }
        if (std::memcmp(pkt.data(), key_id_.data(), 8) != 0) { why = "auth_key_id mismatch"; break; }
        Bytes plain = crypto::decrypt_packet(sess_.auth_key, pkt.data(), pkt.size(), false);
        if (plain.empty()) { why = "msg_key verification failed"; break; }
        try { handle_packet(plain); }
        catch (const std::exception& e) { log::warn(std::string("MTProto: bad packet: ") + e.what()); }
    }
    connected_ = false;
    if (!stopping_) log::warn("MTProto: reader stopped (" + why + ")");
    fail_all(why);
    int old;
    { std::lock_guard<std::mutex> lk(send_mu_); old = fd_; fd_ = -1; }
    if (old >= 0) ::close(old);
}

void Client::handle_packet(const Bytes& plain) {
    if (plain.size() < 32) return;
    std::int64_t session = get_i64(plain.data() + 8), msg_id = get_i64(plain.data() + 16);
    std::int32_t seq = get_i32(plain.data() + 24), len = get_i32(plain.data() + 28);
    if (session != session_id_) return;
    if (len < 0 || static_cast<std::size_t>(len) > plain.size() - 32) return;
    std::vector<std::int64_t> acks;
    process(plain.data() + 32, static_cast<std::size_t>(len), msg_id, seq, acks);
    send_ack(acks);
}

void Client::process(const std::uint8_t* d, std::size_t n, std::int64_t msg_id, std::int32_t seq, std::vector<std::int64_t>& acks) {
    if (n < 4) return;
    if (seq & 1) acks.push_back(msg_id);
    std::uint32_t id = get_u32(d);
    switch (id) {
    case kContainer: {
        if (n < 8) return;
        std::int32_t cnt = get_i32(d + 4);
        std::size_t pos = 8;
        for (std::int32_t i = 0; i < cnt; ++i) {
            if (n - pos < 16) return;
            std::int64_t mid = get_i64(d + pos);
            std::int32_t sq = get_i32(d + pos + 8), bytes = get_i32(d + pos + 12);
            pos += 16;
            if (bytes < 0 || static_cast<std::size_t>(bytes) > n - pos) return;
            process(d + pos, static_cast<std::size_t>(bytes), mid, sq, acks);
            pos += static_cast<std::size_t>(bytes);
        }
        return;
    }
    case kRpcResult:
        if (n >= 12) complete(get_i64(d + 4), d + 12, n - 12);
        return;
    case kGzip: {
        std::size_t pos = 4;
        // gzip_packed is a TL "bytes" value
        std::size_t len = d[pos++];
        if (len == 254) { len = d[pos] | (d[pos + 1] << 8) | (d[pos + 2] << 16); pos += 3; }
        if (pos + len > n) return;
        Bytes plain = gunzip(d + pos, len);
        process(plain.data(), plain.size(), msg_id, 0, acks);
        return;
    }
    case kBadSalt: {
        if (n < 28) return;
        std::int64_t bad = get_i64(d + 4), new_salt = get_i64(d + 20);
        { std::lock_guard<std::mutex> lk(send_mu_); salt_ = new_salt; }
        resend(bad);
        return;
    }
    case kBadMsg: {
        if (n < 20) return;
        std::int64_t bad = get_i64(d + 4);
        std::int32_t code = get_i32(d + 16);
        if (code == 16 || code == 17) {
            // our clock is off: derive the offset from the server's msg_id
            std::int64_t server_us = static_cast<std::int64_t>(static_cast<std::uint64_t>(msg_id) >> 32) * 1000000;
            time_offset_us_ = server_us - now_us();
            log::info("MTProto: clock corrected by " + std::to_string(time_offset_us_ / 1000000) + "s");
            { std::lock_guard<std::mutex> lk(send_mu_); last_msg_id_ = 0; }
            resend(bad);
        } else if (code == 32 || code == 33) {
            { std::lock_guard<std::mutex> lk(send_mu_); seq_ += code == 32 ? 64 : 0; if (code == 33 && seq_ > 16) seq_ -= 16; }
            resend(bad);
        } else if (code == 48) {
            resend(bad);
        } else {
            PendingPtr p;
            { std::lock_guard<std::mutex> lk(mu_); auto it = pending_.find(bad); if (it != pending_.end()) { p = it->second; pending_.erase(it); } }
            if (p) p->done.set_exception(std::make_exception_ptr(TransportError("bad_msg_notification code " + std::to_string(code))));
        }
        return;
    }
    case kNewSession:
        if (n >= 28) { std::lock_guard<std::mutex> lk(send_mu_); salt_ = get_i64(d + 20); }
        return;
    case kPong:
        if (n >= 20) complete(get_i64(d + 4), d, n);   // pong answers `ping` by msg_id and is not wrapped in rpc_result
        return;
    case kMsgsAck:
        return;
    default:
        break;
    }
    if (on_update) {
        try { on_update(schema_.deserialize("Updates", d, n)); } catch (const std::exception&) {}
    }
}

void Client::resend(std::int64_t old_id) {
    PendingPtr p;
    {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = pending_.find(old_id);
        if (it == pending_.end()) return;
        p = it->second;
        pending_.erase(it);
    }
    try {
        std::lock_guard<std::mutex> lk(mu_);
        std::int64_t nid = 0;
        send_body(p->body, nid, true);
        pending_[nid] = p;
    } catch (...) {
        try { p->done.set_exception(std::current_exception()); } catch (...) {}
    }
}

void Client::complete(std::int64_t req_id, const std::uint8_t* d, std::size_t n) {
    PendingPtr p;
    {
        std::lock_guard<std::mutex> lk(mu_);
        auto it = pending_.find(req_id);
        if (it == pending_.end()) return;
        p = it->second;
        pending_.erase(it);
    }
    try {
        if (n >= 4 && get_u32(d) == kRpcError) {
            json e = schema_.deserialize("RpcError", d, n);
            p->done.set_exception(std::make_exception_ptr(RpcError(e["error_code"].get<int>(), e["error_message"].get<std::string>())));
            return;
        }
        if (n >= 4 && get_u32(d) == kGzip) {
            // gzip_packed may wrap an rpc_error too
            json inner = schema_.deserialize("Object", d, n);
            if (inner.is_object() && inner.value("_", "") == "rpc_error") {
                p->done.set_exception(std::make_exception_ptr(RpcError(inner["error_code"].get<int>(), inner["error_message"].get<std::string>())));
                return;
            }
        }
        p->done.set_value(schema_.deserialize(p->result_type, d, n));
    } catch (...) {
        try { p->done.set_exception(std::current_exception()); } catch (...) {}
    }
}

}  // namespace bot::mtp
