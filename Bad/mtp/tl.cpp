#include "tl.hpp"
#include <zlib.h>
#include <cstring>
#include <fstream>
#include <sstream>

namespace bot::mtp {

namespace {
constexpr std::uint32_t kVectorId = 0x1cb5c415;
constexpr std::uint32_t kBoolTrue = 0x997275b5;
constexpr std::uint32_t kBoolFalse = 0xbc799737;
constexpr std::uint32_t kGzipPacked = 0x3072cfa1;

// "future_salt" is a bare type; "InputPeer" / "messages.Dialogs" are boxed (the last dotted segment decides)
bool is_bare_type(const std::string& t) {
    size_t dot = t.rfind('.');
    char c = t[dot == std::string::npos ? 0 : dot + 1];
    return std::islower(static_cast<unsigned char>(c)) != 0;
}

[[maybe_unused]] bool is_builtin(const std::string& t) {
    return t == "int" || t == "long" || t == "double" || t == "string" || t == "bytes" || t == "int128" ||
           t == "int256" || t == "Bool" || t == "true" || t == "#";
}

std::string trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n"), b = s.find_last_not_of(" \t\r\n");
    return a == std::string::npos ? "" : s.substr(a, b - a + 1);
}

}  // namespace

Bytes gunzip(const std::uint8_t* d, std::size_t n) {
    z_stream s{};
    if (inflateInit2(&s, 32 + MAX_WBITS) != Z_OK) throw TlError("inflateInit failed");
    s.next_in = const_cast<Bytef*>(d);
    s.avail_in = static_cast<uInt>(n);
    Bytes out;
    std::uint8_t buf[16384];
    int rc;
    do {
        s.next_out = buf;
        s.avail_out = sizeof buf;
        rc = inflate(&s, Z_NO_FLUSH);
        if (rc != Z_OK && rc != Z_STREAM_END) { inflateEnd(&s); throw TlError("gzip_packed: inflate failed"); }
        out.insert(out.end(), buf, buf + (sizeof buf - s.avail_out));
        if (out.size() > (64u << 20)) { inflateEnd(&s); throw TlError("gzip_packed: too large"); }
    } while (rc != Z_STREAM_END);
    inflateEnd(&s);
    return out;
}


// ---------------------------------------------------------------- primitives
void put_i32(Bytes& b, std::int32_t v) { for (int i = 0; i < 4; ++i) b.push_back(static_cast<std::uint8_t>(static_cast<std::uint32_t>(v) >> (8 * i))); }
void put_i64(Bytes& b, std::int64_t v) { for (int i = 0; i < 8; ++i) b.push_back(static_cast<std::uint8_t>(static_cast<std::uint64_t>(v) >> (8 * i))); }
void put_tl_bytes(Bytes& b, const std::uint8_t* d, std::size_t n) {
    std::size_t hdr;
    if (n < 254) { b.push_back(static_cast<std::uint8_t>(n)); hdr = 1; }
    else { b.push_back(254); b.push_back(n & 0xff); b.push_back((n >> 8) & 0xff); b.push_back((n >> 16) & 0xff); hdr = 4; }
    b.insert(b.end(), d, d + n);
    std::size_t total = hdr + n;
    while (total % 4) { b.push_back(0); ++total; }
}
std::int32_t get_i32(const std::uint8_t* p) { std::int32_t v; std::memcpy(&v, p, 4); return v; }
std::int64_t get_i64(const std::uint8_t* p) { std::int64_t v; std::memcpy(&v, p, 8); return v; }
std::uint32_t get_u32(const std::uint8_t* p) { std::uint32_t v; std::memcpy(&v, p, 4); return v; }

// ---------------------------------------------------------------- schema loading
void Schema::load_file(const std::string& path) {
    std::ifstream f(path);
    if (!f) throw TlError("cannot open TL schema: " + path);
    std::stringstream ss;
    ss << f.rdbuf();
    load_text(ss.str());
}

void Schema::load_text(const std::string& text) {
    std::istringstream in(text);
    std::string line;
    bool functions = false;
    std::string acc;
    while (std::getline(in, line)) {
        std::string t = trim(line);
        if (t.rfind("//", 0) == 0 || t.empty()) continue;
        if (t == "---functions---") { functions = true; continue; }
        if (t == "---types---") { functions = false; continue; }
        acc += t;
        if (acc.back() != ';') { acc += ' '; continue; }   // declaration continues on the next line
        std::string decl = acc.substr(0, acc.size() - 1);
        acc.clear();

        size_t eq = decl.rfind(" = ");
        if (eq == std::string::npos) continue;
        std::string lhs = trim(decl.substr(0, eq)), result = trim(decl.substr(eq + 3));
        std::istringstream ls(lhs);
        std::string head;
        ls >> head;
        size_t hash = head.find('#');
        if (hash == std::string::npos) continue;           // no constructor id
        TlCtor c;
        c.name = head.substr(0, hash);
        try { c.id = static_cast<std::uint32_t>(std::stoul(head.substr(hash + 1), nullptr, 16)); } catch (...) { continue; }
        if (c.name == "vector") continue;                  // builtin
        c.result = result;
        c.is_function = functions;
        std::string tok;
        while (ls >> tok) {
            if (tok[0] == '{') continue;                   // generic declaration {X:Type}
            size_t colon = tok.find(':');
            if (colon == std::string::npos) continue;      // e.g. "#", "[", "t", "]"
            TlParam p;
            p.name = tok.substr(0, colon);
            std::string ty = tok.substr(colon + 1);
            if (ty == "#") p.is_flags = true;
            size_t q = ty.find('?');
            if (q != std::string::npos) {
                std::string cond = ty.substr(0, q);
                size_t dot = cond.find('.');
                if (dot == std::string::npos) continue;
                p.flag_field = cond.substr(0, dot);
                p.flag_bit = std::stoi(cond.substr(dot + 1));
                ty = ty.substr(q + 1);
            }
            p.type = ty;
            c.params.push_back(std::move(p));
        }
        by_name_[c.name] = c.id;   // later definitions (functions section) override types on a clash
        by_id_[c.id] = std::move(c);
    }
}

const TlCtor* Schema::by_id(std::uint32_t id) const {
    auto it = by_id_.find(id);
    return it == by_id_.end() ? nullptr : &it->second;
}
const TlCtor* Schema::by_name(const std::string& n) const {
    auto it = by_name_.find(n);
    return it == by_name_.end() ? nullptr : by_id(it->second);
}

// ---------------------------------------------------------------- writer
Bytes Schema::serialize(const json& obj) const {
    Bytes out;
    const TlCtor* c = by_name(obj.value("_", std::string()));
    if (!c) throw TlError("unknown TL constructor: " + obj.value("_", std::string()));
    put_i32(out, static_cast<std::int32_t>(c->id));
    write_ctor_body(out, *c, obj);
    return out;
}

void Schema::write_ctor_body(Bytes& out, const TlCtor& c, const json& obj) const {
    std::map<std::string, std::uint32_t> flags;
    for (const auto& p : c.params) {
        if (p.flag_bit < 0) continue;
        auto it = obj.find(p.name);
        bool present = it != obj.end() && !it->is_null();
        if (present && p.type == "true") present = it->is_boolean() ? it->get<bool>() : true;
        if (present) flags[p.flag_field] |= (1u << p.flag_bit);
    }
    for (const auto& p : c.params) {
        if (p.is_flags) { put_i32(out, static_cast<std::int32_t>(flags[p.name])); continue; }
        auto it = obj.find(p.name);
        bool present = it != obj.end() && !it->is_null();
        if (p.flag_bit >= 0) {
            if (!(flags[p.flag_field] & (1u << p.flag_bit))) continue;
            if (p.type == "true") continue;   // flag only
        } else if (!present) {
            throw TlError("missing field '" + p.name + "' for " + c.name);
        }
        write_value(out, p.type, *it);
    }
}

void Schema::write_value(Bytes& out, const std::string& type, const json& v) const {
    if (type == "int") { put_i32(out, static_cast<std::int32_t>(v.get<std::int64_t>())); return; }
    if (type == "long") { put_i64(out, v.is_number_unsigned() ? static_cast<std::int64_t>(v.get<std::uint64_t>()) : v.get<std::int64_t>()); return; }
    if (type == "double") { double d = v.get<double>(); std::uint8_t b[8]; std::memcpy(b, &d, 8); out.insert(out.end(), b, b + 8); return; }
    if (type == "string") { const auto& s = v.get_ref<const std::string&>(); put_tl_bytes(out, reinterpret_cast<const std::uint8_t*>(s.data()), s.size()); return; }
    if (type == "bytes") {
        if (v.is_binary()) { const auto& b = v.get_binary(); put_tl_bytes(out, b.data(), b.size()); }
        else { const auto& s = v.get_ref<const std::string&>(); put_tl_bytes(out, reinterpret_cast<const std::uint8_t*>(s.data()), s.size()); }
        return;
    }
    if (type == "int128" || type == "int256") {
        const auto& b = v.get_binary();
        if (b.size() != (type == "int128" ? 16u : 32u)) throw TlError("bad " + type + " length");
        out.insert(out.end(), b.begin(), b.end());
        return;
    }
    if (type == "Bool") { put_i32(out, static_cast<std::int32_t>(v.get<bool>() ? kBoolTrue : kBoolFalse)); return; }
    if (type == "true") return;
    if (type == "!X") { Bytes b = serialize(v); out.insert(out.end(), b.begin(), b.end()); return; }
    if (type.rfind("Vector<", 0) == 0 || type.rfind("vector<", 0) == 0) {
        std::string inner = type.substr(7, type.size() - 8);
        if (type[0] == 'V') put_i32(out, static_cast<std::int32_t>(kVectorId));
        put_i32(out, static_cast<std::int32_t>(v.size()));
        for (const auto& e : v) write_value(out, inner, e);
        return;
    }
    // a constructor name (v is an object with "_") or a bare type name
    if (v.is_object() && v.contains("_")) {
        const TlCtor* c = by_name(v["_"].get<std::string>());
        if (!c) throw TlError("unknown TL constructor: " + v["_"].get<std::string>());
        if (!is_bare_type(type)) put_i32(out, static_cast<std::int32_t>(c->id));
        write_ctor_body(out, *c, v);
        return;
    }
    throw TlError("cannot serialize value as " + type);
}

// ---------------------------------------------------------------- reader
struct Schema::Reader {
    const std::uint8_t* d;
    std::size_t n, pos = 0;
    void need(std::size_t k) const { if (n - pos < k) throw TlError("TL: unexpected end of data"); }
    std::int32_t i32() { need(4); auto v = get_i32(d + pos); pos += 4; return v; }
    std::uint32_t u32() { need(4); auto v = get_u32(d + pos); pos += 4; return v; }
    std::int64_t i64() { need(8); auto v = get_i64(d + pos); pos += 8; return v; }
    Bytes raw(std::size_t k) { need(k); Bytes b(d + pos, d + pos + k); pos += k; return b; }
    Bytes tlbytes() {
        need(1);
        std::size_t len = d[pos++], hdr = 1;
        if (len == 254) { need(3); len = d[pos] | (d[pos + 1] << 8) | (d[pos + 2] << 16); pos += 3; hdr = 4; }
        else if (len == 255) throw TlError("TL: bad string length");
        Bytes b = raw(len);
        std::size_t total = hdr + len;
        while (total % 4) { need(1); ++pos; ++total; }
        return b;
    }
};

json Schema::read_ctor_body(Reader& r, const TlCtor& c) const {
    json o = json::object();
    o["_"] = c.name;
    std::map<std::string, std::uint32_t> flags;
    for (const auto& p : c.params) {
        if (p.is_flags) { auto f = r.u32(); flags[p.name] = f; o[p.name] = f; continue; }
        if (p.flag_bit >= 0) {
            bool on = (flags[p.flag_field] >> p.flag_bit) & 1u;
            if (p.type == "true") { if (on) o[p.name] = true; continue; }
            if (!on) continue;
        }
        o[p.name] = read_value(r, p.type);
    }
    return o;
}

json Schema::read_value(Reader& r, const std::string& type) const {
    if (type == "int") return r.i32();
    if (type == "long") return r.i64();
    if (type == "double") { auto b = r.raw(8); double d; std::memcpy(&d, b.data(), 8); return d; }
    if (type == "string") { auto b = r.tlbytes(); return std::string(b.begin(), b.end()); }
    if (type == "bytes") return json::binary(r.tlbytes());
    if (type == "int128") return json::binary(r.raw(16));
    if (type == "int256") return json::binary(r.raw(32));
    if (type == "Bool") {
        auto id = r.u32();
        if (id == kBoolTrue) return true;
        if (id == kBoolFalse) return false;
        throw TlError("TL: bad Bool");
    }
    if (type == "true") return true;
    if (type.rfind("Vector<", 0) == 0 || type.rfind("vector<", 0) == 0) {
        std::string inner = type.substr(7, type.size() - 8);
        if (type[0] == 'V') { auto id = r.u32(); if (id != kVectorId) throw TlError("TL: expected vector id"); }
        std::int32_t cnt = r.i32();
        if (cnt < 0 || static_cast<std::size_t>(cnt) > r.n) throw TlError("TL: bad vector length");
        json a = json::array();
        for (std::int32_t i = 0; i < cnt; ++i) a.push_back(read_value(r, inner));
        return a;
    }
    if (type == "Object" || type == "X" || type == "!X") {
        auto id = r.u32();
        if (id == kGzipPacked) {
            Bytes z = r.tlbytes(), plain = gunzip(z.data(), z.size());
            Reader r2{plain.data(), plain.size()};
            return read_value(r2, "Object");
        }
        if (id == kBoolTrue) return true;
        if (id == kBoolFalse) return false;
        const TlCtor* c = by_id(id);
        if (!c) { std::ostringstream e; e << "TL: unknown constructor 0x" << std::hex << id; throw TlError(e.str()); }
        return read_ctor_body(r, *c);
    }
    if (is_bare_type(type)) {
        const TlCtor* c = by_name(type);
        if (!c) throw TlError("TL: unknown bare type " + type);
        return read_ctor_body(r, *c);
    }
    // boxed type
    auto id = r.u32();
    if (id == kGzipPacked) {
        Bytes z = r.tlbytes(), plain = gunzip(z.data(), z.size());
        Reader r2{plain.data(), plain.size()};
        return read_value(r2, type);
    }
    const TlCtor* c = by_id(id);
    if (!c) { std::ostringstream e; e << "TL: unknown constructor 0x" << std::hex << id << " while reading " << type; throw TlError(e.str()); }
    return read_ctor_body(r, *c);
}

json Schema::deserialize(const std::string& type, const std::uint8_t* data, std::size_t len, std::size_t* consumed) const {
    Reader r{data, len};
    json v = read_value(r, type);
    if (consumed) *consumed = r.pos;
    return v;
}

std::string Schema::result_type(const json& req) const {
    const TlCtor* c = by_name(req.value("_", std::string()));
    if (!c) throw TlError("unknown request: " + req.value("_", std::string()));
    if (c->result == "X" && req.contains("query")) return result_type(req["query"]);
    return c->result;
}

}  // namespace bot::mtp
