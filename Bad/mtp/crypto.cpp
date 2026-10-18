#include "crypto.hpp"
#include <cerrno>
#include <sys/random.h>
#include <unistd.h>
#include <cstring>
#include <memory>
#include <stdexcept>

namespace bot::mtp::crypto {

// Self-contained primitives on purpose: libntgcalls statically embeds BoringSSL with the *same* symbol names as
// OpenSSL, so linking OpenSSL into this binary would make libntgcalls call into the wrong library and crash.
namespace {
inline std::uint32_t rol(std::uint32_t x, int n) { return (x << n) | (x >> (32 - n)); }
inline std::uint32_t ror(std::uint32_t x, int n) { return (x >> n) | (x << (32 - n)); }

template <class Block>
void md_run(const std::uint8_t* d, std::size_t n, Block block, std::uint32_t* h, std::size_t nh, Bytes& out) {
    std::uint8_t buf[128];
    std::size_t i = 0;
    for (; i + 64 <= n; i += 64) block(h, d + i);
    std::size_t rem = n - i;
    std::memset(buf, 0, sizeof buf);
    if (rem) std::memcpy(buf, d + i, rem);
    buf[rem] = 0x80;
    std::size_t tot = rem + 1 + 8 <= 64 ? 64 : 128;
    std::uint64_t bits = static_cast<std::uint64_t>(n) * 8;
    for (int k = 0; k < 8; ++k) buf[tot - 1 - k] = static_cast<std::uint8_t>(bits >> (8 * k));
    block(h, buf);
    if (tot == 128) block(h, buf + 64);
    out.resize(nh * 4);
    for (std::size_t k = 0; k < nh; ++k)
        for (int j = 0; j < 4; ++j) out[k * 4 + j] = static_cast<std::uint8_t>(h[k] >> (24 - 8 * j));
}

void sha1_block(std::uint32_t* h, const std::uint8_t* p) {
    std::uint32_t w[80];
    for (int i = 0; i < 16; ++i) w[i] = (std::uint32_t(p[4*i]) << 24) | (std::uint32_t(p[4*i+1]) << 16) | (std::uint32_t(p[4*i+2]) << 8) | p[4*i+3];
    for (int i = 16; i < 80; ++i) w[i] = rol(w[i-3] ^ w[i-8] ^ w[i-14] ^ w[i-16], 1);
    std::uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4];
    for (int i = 0; i < 80; ++i) {
        std::uint32_t f, k;
        if (i < 20) { f = (b & c) | (~b & d); k = 0x5A827999; }
        else if (i < 40) { f = b ^ c ^ d; k = 0x6ED9EBA1; }
        else if (i < 60) { f = (b & c) | (b & d) | (c & d); k = 0x8F1BBCDC; }
        else { f = b ^ c ^ d; k = 0xCA62C1D6; }
        std::uint32_t t = rol(a, 5) + f + e + k + w[i];
        e = d; d = c; c = rol(b, 30); b = a; a = t;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e;
}

const std::uint32_t K256[64] = {
    0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
    0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
    0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
    0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};

void sha256_block(std::uint32_t* h, const std::uint8_t* p) {
    std::uint32_t w[64];
    for (int i = 0; i < 16; ++i) w[i] = (std::uint32_t(p[4*i]) << 24) | (std::uint32_t(p[4*i+1]) << 16) | (std::uint32_t(p[4*i+2]) << 8) | p[4*i+3];
    for (int i = 16; i < 64; ++i) {
        std::uint32_t s0 = ror(w[i-15], 7) ^ ror(w[i-15], 18) ^ (w[i-15] >> 3);
        std::uint32_t s1 = ror(w[i-2], 17) ^ ror(w[i-2], 19) ^ (w[i-2] >> 10);
        w[i] = w[i-16] + s0 + w[i-7] + s1;
    }
    std::uint32_t a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh = h[7];
    for (int i = 0; i < 64; ++i) {
        std::uint32_t S1 = ror(e, 6) ^ ror(e, 11) ^ ror(e, 25), ch = (e & f) ^ (~e & g);
        std::uint32_t t1 = hh + S1 + ch + K256[i] + w[i];
        std::uint32_t S0 = ror(a, 2) ^ ror(a, 13) ^ ror(a, 22), mj = (a & b) ^ (a & c) ^ (b & c);
        std::uint32_t t2 = S0 + mj;
        hh = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
    }
    h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh;
}
}  // namespace

Bytes sha1(const std::uint8_t* d, std::size_t n) {
    std::uint32_t h[5] = {0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0};
    Bytes o;
    md_run(d, n, sha1_block, h, 5, o);
    return o;
}
Bytes sha256(const std::uint8_t* d, std::size_t n) {
    std::uint32_t h[8] = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    Bytes o;
    md_run(d, n, sha256_block, h, 8, o);
    return o;
}
void random_bytes(std::uint8_t* out, std::size_t n) {
    std::size_t got = 0;
    while (got < n) {
        ssize_t r = getrandom(out + got, n - got, 0);
        if (r < 0) { if (errno == EINTR) continue; throw std::runtime_error("getrandom failed"); }
        got += static_cast<std::size_t>(r);
    }
}
std::uint64_t random_u64() { std::uint64_t v; random_bytes(reinterpret_cast<std::uint8_t*>(&v), 8); return v; }

Bytes auth_key_id(const Bytes& k) {
    Bytes h = sha1(k.data(), k.size());
    return Bytes(h.begin() + 12, h.begin() + 20);
}

namespace {
// AES-256 (FIPS-197), table-free byte implementation.
struct Aes {
    std::uint8_t sbox[256], inv[256];
    Aes() {
        std::uint8_t p = 1, q = 1;
        do {
            p = static_cast<std::uint8_t>(p ^ (p << 1) ^ ((p & 0x80) ? 0x1B : 0));
            q ^= static_cast<std::uint8_t>(q << 1); q ^= static_cast<std::uint8_t>(q << 2); q ^= static_cast<std::uint8_t>(q << 4);
            if (q & 0x80) q ^= 0x09;
            std::uint8_t x = static_cast<std::uint8_t>(q ^ (q << 1 | q >> 7) ^ (q << 2 | q >> 6) ^ (q << 3 | q >> 5) ^ (q << 4 | q >> 4));
            sbox[p] = static_cast<std::uint8_t>(x ^ 0x63);
        } while (p != 1);
        sbox[0] = 0x63;
        for (int i = 0; i < 256; ++i) inv[sbox[i]] = static_cast<std::uint8_t>(i);
    }
};
const Aes& aes_tables() { static const Aes t; return t; }
inline std::uint8_t xt(std::uint8_t x) { return static_cast<std::uint8_t>((x << 1) ^ ((x & 0x80) ? 0x1B : 0)); }
inline std::uint8_t mul(std::uint8_t x, std::uint8_t y) {
    std::uint8_t r = 0;
    while (y) { if (y & 1) r ^= x; x = xt(x); y >>= 1; }
    return r;
}

struct Ctx {
    std::uint8_t rk[15][16];
    bool enc;
    Ctx(const std::uint8_t* key, bool e) : enc(e) {
        const auto& T = aes_tables();
        std::uint8_t w[240];
        std::memcpy(w, key, 32);
        std::uint8_t rcon = 1;
        for (int i = 32; i < 240; i += 4) {
            std::uint8_t t[4] = {w[i-4], w[i-3], w[i-2], w[i-1]};
            if (i % 32 == 0) {
                std::uint8_t t0 = t[0];
                t[0] = static_cast<std::uint8_t>(T.sbox[t[1]] ^ rcon); t[1] = T.sbox[t[2]]; t[2] = T.sbox[t[3]]; t[3] = T.sbox[t0];
                rcon = xt(rcon);
            } else if (i % 32 == 16) {
                for (auto& b : t) b = T.sbox[b];
            }
            for (int j = 0; j < 4; ++j) w[i + j] = static_cast<std::uint8_t>(w[i - 32 + j] ^ t[j]);
        }
        for (int r = 0; r < 15; ++r) std::memcpy(rk[r], w + 16 * r, 16);
    }
    void block(const std::uint8_t* in, std::uint8_t* out) {
        const auto& T = aes_tables();
        std::uint8_t s[16];
        for (int i = 0; i < 16; ++i) s[i] = in[i];
        auto add = [&](int r) { for (int i = 0; i < 16; ++i) s[i] ^= rk[r][i]; };
        if (enc) {
            add(0);
            for (int r = 1; r <= 14; ++r) {
                std::uint8_t t[16];
                for (int i = 0; i < 16; ++i) t[i] = T.sbox[s[i]];
                // shift rows (state is column-major: index = 4*col + row)
                for (int c = 0; c < 4; ++c) for (int rr = 0; rr < 4; ++rr) s[4*c + rr] = t[4*((c + rr) % 4) + rr];
                if (r != 14) {
                    for (int c = 0; c < 4; ++c) {
                        std::uint8_t* col = s + 4*c;
                        std::uint8_t a0 = col[0], a1 = col[1], a2 = col[2], a3 = col[3];
                        col[0] = static_cast<std::uint8_t>(xt(a0) ^ (xt(a1) ^ a1) ^ a2 ^ a3);
                        col[1] = static_cast<std::uint8_t>(a0 ^ xt(a1) ^ (xt(a2) ^ a2) ^ a3);
                        col[2] = static_cast<std::uint8_t>(a0 ^ a1 ^ xt(a2) ^ (xt(a3) ^ a3));
                        col[3] = static_cast<std::uint8_t>((xt(a0) ^ a0) ^ a1 ^ a2 ^ xt(a3));
                    }
                }
                add(r);
            }
        } else {
            add(14);
            for (int r = 13; r >= 0; --r) {
                std::uint8_t t[16];
                // inverse shift rows
                for (int c = 0; c < 4; ++c) for (int rr = 0; rr < 4; ++rr) t[4*((c + rr) % 4) + rr] = s[4*c + rr];
                for (int i = 0; i < 16; ++i) s[i] = T.inv[t[i]];
                add(r);
                if (r != 0) {
                    for (int c = 0; c < 4; ++c) {
                        std::uint8_t* col = s + 4*c;
                        std::uint8_t a0 = col[0], a1 = col[1], a2 = col[2], a3 = col[3];
                        col[0] = static_cast<std::uint8_t>(mul(a0,14) ^ mul(a1,11) ^ mul(a2,13) ^ mul(a3,9));
                        col[1] = static_cast<std::uint8_t>(mul(a0,9) ^ mul(a1,14) ^ mul(a2,11) ^ mul(a3,13));
                        col[2] = static_cast<std::uint8_t>(mul(a0,13) ^ mul(a1,9) ^ mul(a2,14) ^ mul(a3,11));
                        col[3] = static_cast<std::uint8_t>(mul(a0,11) ^ mul(a1,13) ^ mul(a2,9) ^ mul(a3,14));
                    }
                }
            }
        }
        std::memcpy(out, s, 16);
    }
};
}  // namespace

// IGE: y_i = E(x_i ^ y_{i-1}) ^ x_{i-1};  iv = y_0 (16) || x_0 (16)
void ige_encrypt(const std::uint8_t* in, std::uint8_t* out, std::size_t n, const std::uint8_t key[32], const std::uint8_t iv[32]) {
    if (n % 16) throw std::runtime_error("IGE: length not a multiple of 16");
    Ctx c(key, true);
    std::uint8_t y_prev[16], x_prev[16], tmp[16];
    std::memcpy(y_prev, iv, 16);
    std::memcpy(x_prev, iv + 16, 16);
    for (std::size_t i = 0; i < n; i += 16) {
        std::uint8_t x[16];
        std::memcpy(x, in + i, 16);   // in == out is allowed
        for (int j = 0; j < 16; ++j) tmp[j] = x[j] ^ y_prev[j];
        c.block(tmp, tmp);
        for (int j = 0; j < 16; ++j) tmp[j] ^= x_prev[j];
        std::memcpy(out + i, tmp, 16);
        std::memcpy(y_prev, tmp, 16);
        std::memcpy(x_prev, x, 16);
    }
}
void ige_decrypt(const std::uint8_t* in, std::uint8_t* out, std::size_t n, const std::uint8_t key[32], const std::uint8_t iv[32]) {
    if (n % 16) throw std::runtime_error("IGE: length not a multiple of 16");
    Ctx c(key, false);
    std::uint8_t y_prev[16], x_prev[16], tmp[16];
    std::memcpy(y_prev, iv, 16);
    std::memcpy(x_prev, iv + 16, 16);
    for (std::size_t i = 0; i < n; i += 16) {
        std::uint8_t y[16];
        std::memcpy(y, in + i, 16);
        for (int j = 0; j < 16; ++j) tmp[j] = y[j] ^ x_prev[j];
        c.block(tmp, tmp);
        for (int j = 0; j < 16; ++j) tmp[j] ^= y_prev[j];
        std::memcpy(out + i, tmp, 16);
        std::memcpy(x_prev, tmp, 16);
        std::memcpy(y_prev, y, 16);
    }
}

namespace {
void derive(const Bytes& k, const std::uint8_t msg_key[16], int x, std::uint8_t aes_key[32], std::uint8_t aes_iv[32]) {
    Bytes a_in(msg_key, msg_key + 16);
    a_in.insert(a_in.end(), k.begin() + x, k.begin() + x + 36);
    Bytes b_in(k.begin() + 40 + x, k.begin() + 40 + x + 36);
    b_in.insert(b_in.end(), msg_key, msg_key + 16);
    Bytes a = sha256(a_in.data(), a_in.size()), b = sha256(b_in.data(), b_in.size());
    std::memcpy(aes_key, a.data(), 8);
    std::memcpy(aes_key + 8, b.data() + 8, 16);
    std::memcpy(aes_key + 24, a.data() + 24, 8);
    std::memcpy(aes_iv, b.data(), 8);
    std::memcpy(aes_iv + 8, a.data() + 8, 16);
    std::memcpy(aes_iv + 24, b.data() + 24, 8);
}
Bytes msg_key_of(const Bytes& k, int x, const std::uint8_t* plain, std::size_t n) {
    Bytes in(k.begin() + 88 + x, k.begin() + 88 + x + 32);
    in.insert(in.end(), plain, plain + n);
    Bytes h = sha256(in.data(), in.size());
    return Bytes(h.begin() + 8, h.begin() + 24);
}
}  // namespace

Bytes encrypt_packet(const Bytes& k, const Bytes& key_id, const Bytes& plain, bool from_client) {
    int x = from_client ? 0 : 8;
    Bytes mk = msg_key_of(k, x, plain.data(), plain.size());
    std::uint8_t ak[32], iv[32];
    derive(k, mk.data(), x, ak, iv);
    Bytes out(key_id);
    out.insert(out.end(), mk.begin(), mk.end());
    out.resize(24 + plain.size());
    ige_encrypt(plain.data(), out.data() + 24, plain.size(), ak, iv);
    return out;
}

Bytes decrypt_packet(const Bytes& k, const std::uint8_t* pkt, std::size_t n, bool from_client) {
    if (n < 24 + 32 || (n - 24) % 16) return {};
    int x = from_client ? 0 : 8;   // x is chosen by the *sender*: client->server 0, server->client 8
    const std::uint8_t* mk = pkt + 8;
    std::uint8_t ak[32], iv[32];
    derive(k, mk, x, ak, iv);
    Bytes plain(n - 24);
    ige_decrypt(pkt + 24, plain.data(), plain.size(), ak, iv);
    Bytes want = msg_key_of(k, x, plain.data(), plain.size());
    if (std::memcmp(want.data(), mk, 16) != 0) return {};
    return plain;
}

}  // namespace bot::mtp::crypto
