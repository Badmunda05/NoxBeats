// Generic schema-driven TL (Type Language) codec.
// Loads Telegram's api.tl / mtproto.tl at runtime and converts between binary TL and JSON:
//   {"_": "inputGroupCall", "id": 1, "access_hash": 2}
// Flags are computed from the presence of fields; bytes/int128/int256 are json binary values.
#pragma once
#include <cstdint>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>
#include <nlohmann/json.hpp>

namespace bot::mtp {

using json = nlohmann::json;
using Bytes = std::vector<std::uint8_t>;

struct TlError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

struct TlParam {
    std::string name;
    std::string type;        // base type, e.g. "int", "Vector<User>", "InputPeer"
    std::string flag_field;  // "flags" / "flags2" when conditional, else empty
    int flag_bit = -1;
    bool is_flags = false;   // type "#"
};

struct TlCtor {
    std::string name;
    std::uint32_t id = 0;
    std::vector<TlParam> params;
    std::string result;      // result / parent type
    bool is_function = false;
};

class Schema {
public:
    void load_file(const std::string& path);        // throws TlError
    void load_text(const std::string& text);
    const TlCtor* by_id(std::uint32_t id) const;
    const TlCtor* by_name(const std::string& name) const;   // functions win over types on a name clash
    std::size_t size() const { return by_id_.size(); }

    // Serialize a boxed object ({"_": ...}); throws TlError on schema violations.
    Bytes serialize(const json& obj) const;
    // Deserialize a value of the given TL type ("Updates", "Vector<User>", "int", ...). `consumed` optional.
    json deserialize(const std::string& type, const std::uint8_t* data, std::size_t len, std::size_t* consumed = nullptr) const;
    // Result type of a request object (resolves invokeWithLayer / initConnection wrappers).
    std::string result_type(const json& request) const;

private:
    struct Reader;
    void write_value(Bytes& out, const std::string& type, const json& v) const;
    void write_ctor_body(Bytes& out, const TlCtor& c, const json& obj) const;
    json read_value(Reader& r, const std::string& type) const;
    json read_ctor_body(Reader& r, const TlCtor& c) const;
    std::unordered_map<std::uint32_t, TlCtor> by_id_;
    std::unordered_map<std::string, std::uint32_t> by_name_;
};

// primitive helpers (also used by the transport layer)
Bytes gunzip(const std::uint8_t* d, std::size_t n);   // throws TlError
void put_i32(Bytes& b, std::int32_t v);
void put_i64(Bytes& b, std::int64_t v);
void put_tl_bytes(Bytes& b, const std::uint8_t* d, std::size_t n);   // TL string/bytes encoding
std::int32_t get_i32(const std::uint8_t* p);
std::int64_t get_i64(const std::uint8_t* p);
std::uint32_t get_u32(const std::uint8_t* p);

}  // namespace bot::mtp
