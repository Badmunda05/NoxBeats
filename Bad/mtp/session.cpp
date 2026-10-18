#include "session.hpp"
#include <arpa/inet.h>
#include <algorithm>
#include <map>

namespace bot::mtp {

std::vector<std::uint8_t> base64url_decode(std::string in) {
    std::vector<std::uint8_t> out;
    int val = 0, bits = -8;
    for (unsigned char c : in) {
        int v;
        if (c >= 'A' && c <= 'Z') v = c - 'A';
        else if (c >= 'a' && c <= 'z') v = c - 'a' + 26;
        else if (c >= '0' && c <= '9') v = c - '0' + 52;
        else if (c == '+' || c == '-') v = 62;
        else if (c == '/' || c == '_') v = 63;
        else if (c == '=') break;
        else continue;
        val = (val << 6) | v;
        bits += 6;
        if (bits >= 0) { out.push_back(static_cast<std::uint8_t>((val >> bits) & 0xff)); bits -= 8; }
    }
    return out;
}

namespace {
// primary + alternative addresses of the production data centers
const std::map<int, std::vector<std::string>>& prod_dcs() {
    static const std::map<int, std::vector<std::string>> m = {
        {1, {"149.154.175.53", "149.154.175.50", "149.154.175.58"}},
        {2, {"149.154.167.51", "149.154.167.50", "149.154.167.40"}},
        {3, {"149.154.175.100", "149.154.175.117"}},
        {4, {"149.154.167.91", "149.154.167.92"}},
        {5, {"91.108.56.130", "91.108.56.151", "91.108.56.116"}},
    };
    return m;
}
const std::map<int, std::vector<std::string>>& test_dcs() {
    static const std::map<int, std::vector<std::string>> m = {
        {1, {"149.154.175.10"}}, {2, {"149.154.167.40"}}, {3, {"149.154.175.117"}}};
    return m;
}
void set_addr(SessionData& s) {
    const auto& m = s.test_mode ? test_dcs() : prod_dcs();
    auto it = m.find(s.dc_id);
    if (it == m.end()) it = m.find(2);
    s.ip = it->second.front();
    s.fallback_ips.assign(it->second.begin() + 1, it->second.end());
    s.port = 443;
}
std::uint64_t be(const std::vector<std::uint8_t>& d, size_t off, int n) {
    std::uint64_t v = 0;
    for (int i = 0; i < n; ++i) v = (v << 8) | d[off + static_cast<size_t>(i)];
    return v;
}
}  // namespace

std::optional<SessionData> decode_session(const std::string& str, const std::string& type) {
    std::string s = str;
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' ')) s.pop_back();
    while (!s.empty() && (s.front() == ' ' || s.front() == '"')) s.erase(s.begin());
    if (s.empty()) return std::nullopt;
    SessionData r;
    if (type == "telethon") {
        if (s[0] != '1') return std::nullopt;
        auto d = base64url_decode(s.substr(1));
        size_t ip_len = d.size() == 263 ? 4 : d.size() == 275 ? 16 : 0;
        if (!ip_len) return std::nullopt;
        r.dc_id = d[0];
        char ip[INET6_ADDRSTRLEN] = {};
        inet_ntop(ip_len == 4 ? AF_INET : AF_INET6, d.data() + 1, ip, sizeof ip);
        r.ip = ip;
        r.port = static_cast<int>(be(d, 1 + ip_len, 2));
        r.auth_key.assign(d.begin() + 1 + static_cast<long>(ip_len) + 2, d.begin() + 1 + static_cast<long>(ip_len) + 2 + 256);
        if (ip_len == 16) { set_addr(r); }   // IPv6 sessions: connect over IPv4 instead
        else if (r.port == 0) r.port = 443;
        return r;
    }
    auto d = base64url_decode(s);
    if (d.size() == 271) {                 // >B I ? 256s Q ?
        r.dc_id = d[0];
        r.api_id = static_cast<std::uint32_t>(be(d, 1, 4));
        r.test_mode = d[5] != 0;
        r.auth_key.assign(d.begin() + 6, d.begin() + 262);
        r.user_id = static_cast<std::int64_t>(be(d, 262, 8));
        r.is_bot = d[270] != 0;
    } else if (d.size() == 267) {          // >B I ? 256s I ?
        r.dc_id = d[0];
        r.api_id = static_cast<std::uint32_t>(be(d, 1, 4));
        r.test_mode = d[5] != 0;
        r.auth_key.assign(d.begin() + 6, d.begin() + 262);
        r.user_id = static_cast<std::int64_t>(be(d, 262, 4));
        r.is_bot = d[266] != 0;
    } else if (d.size() == 263) {          // legacy >B ? 256s I ?
        r.dc_id = d[0];
        r.test_mode = d[1] != 0;
        r.auth_key.assign(d.begin() + 2, d.begin() + 258);
        r.user_id = static_cast<std::int64_t>(be(d, 258, 4));
        r.is_bot = d[262] != 0;
    } else {
        return std::nullopt;
    }
    if (r.dc_id < 1 || r.dc_id > 5) return std::nullopt;
    set_addr(r);
    return r;
}

}  // namespace bot::mtp
