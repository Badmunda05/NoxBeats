#include "mongo_store.hpp"
#include <mongoc/mongoc.h>
#include <mutex>

namespace bot {
using nlohmann::json;

namespace {
void init_driver() {
    static std::once_flag once;
    std::call_once(once, [] { mongoc_init(); });
}
std::string bson_err(const bson_error_t& e) { return std::string(e.message) + " (code " + std::to_string(e.code) + ")"; }
}  // namespace

MongoStore::~MongoStore() {
    if (coll_) mongoc_collection_destroy(coll_);
    if (client_) mongoc_client_destroy(client_);
}

bool MongoStore::connect(const std::string& uri_str, std::string* err) {
    init_driver();
    bson_error_t e;
    mongoc_uri_t* uri = mongoc_uri_new_with_error(uri_str.c_str(), &e);
    if (!uri) { if (err) *err = "invalid MONGO_DB_URI: " + bson_err(e); return false; }
    // fail fast instead of hanging for the 30s default when the cluster is unreachable
    if (!mongoc_uri_has_option(uri, MONGOC_URI_SERVERSELECTIONTIMEOUTMS))
        mongoc_uri_set_option_as_int32(uri, MONGOC_URI_SERVERSELECTIONTIMEOUTMS, 10000);
    if (!mongoc_uri_has_option(uri, MONGOC_URI_CONNECTTIMEOUTMS))
        mongoc_uri_set_option_as_int32(uri, MONGOC_URI_CONNECTTIMEOUTMS, 10000);
    const char* dbn = mongoc_uri_get_database(uri);
    db_name_ = (dbn && *dbn) ? dbn : "BadXMusic";
    client_ = mongoc_client_new_from_uri_with_error(uri, &e);
    mongoc_uri_destroy(uri);
    if (!client_) { if (err) *err = "mongo client: " + bson_err(e); return false; }
    mongoc_client_set_appname(client_, "BadXMusic");

    bson_t* ping = BCON_NEW("ping", BCON_INT32(1));
    bool ok = mongoc_client_command_simple(client_, "admin", ping, nullptr, nullptr, &e);
    bson_destroy(ping);
    if (!ok) {
        if (err) *err = bson_err(e);
        mongoc_client_destroy(client_); client_ = nullptr;
        return false;
    }
    coll_ = mongoc_client_get_collection(client_, db_name_.c_str(), "state");
    return true;
}

bool MongoStore::load(std::map<std::string, json>& out, std::string* err) {
    bson_t* filter = bson_new();
    mongoc_cursor_t* cur = mongoc_collection_find_with_opts(coll_, filter, nullptr, nullptr);
    const bson_t* doc;
    while (mongoc_cursor_next(cur, &doc)) {
        char* s = bson_as_relaxed_extended_json(doc, nullptr);
        if (!s) continue;
        json j = json::parse(s, nullptr, false);
        bson_free(s);
        if (j.is_object() && j.contains("_id") && j["_id"].is_string() && j.contains("v"))
            out[j["_id"].get<std::string>()] = j["v"];
    }
    bson_error_t e;
    bool ok = !mongoc_cursor_error(cur, &e);
    if (!ok && err) *err = bson_err(e);
    mongoc_cursor_destroy(cur);
    bson_destroy(filter);
    return ok;
}

bool MongoStore::save(const std::string& key, const json& value, std::string* err) {
    json doc = {{"_id", key}, {"v", value}};
    bson_error_t e;
    bson_t* b = bson_new_from_json(reinterpret_cast<const uint8_t*>(doc.dump().c_str()), -1, &e);
    if (!b) { if (err) *err = "bson: " + bson_err(e); return false; }
    bson_t* sel = BCON_NEW("_id", BCON_UTF8(key.c_str()));
    bson_t* opts = BCON_NEW("upsert", BCON_BOOL(true));
    bool ok = mongoc_collection_replace_one(coll_, sel, b, opts, nullptr, &e);
    if (!ok && err) *err = bson_err(e);
    bson_destroy(opts); bson_destroy(sel); bson_destroy(b);
    return ok;
}

}  // namespace bot
