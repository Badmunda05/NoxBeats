// MongoDB persistence (libmongoc). Every top-level state key is one document {_id: key, v: value} in the
// "state" collection, so the data is plain, queryable BSON (not an opaque blob). Not thread-safe: DB serialises calls.
#pragma once
#include <map>
#include <string>
#include <nlohmann/json.hpp>

struct _mongoc_client_t;
struct _mongoc_collection_t;

namespace bot {

class MongoStore {
public:
    MongoStore() = default;
    ~MongoStore();
    MongoStore(const MongoStore&) = delete;
    MongoStore& operator=(const MongoStore&) = delete;

    // Connects and pings. On failure returns false and fills *err.
    bool connect(const std::string& uri, std::string* err);
    // Reads every document of the state collection into {key: value}.
    bool load(std::map<std::string, nlohmann::json>& out, std::string* err);
    // Upserts one key.
    bool save(const std::string& key, const nlohmann::json& value, std::string* err);
    const std::string& db_name() const { return db_name_; }

private:
    _mongoc_client_t* client_ = nullptr;
    _mongoc_collection_t* coll_ = nullptr;
    std::string db_name_;
};

}  // namespace bot
