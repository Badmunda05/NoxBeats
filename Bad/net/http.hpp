// Thin libcurl wrapper (one reusable easy handle per thread = keep-alive).
#pragma once
#include <atomic>
#include <functional>
#include <map>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
#include "../core/types.hpp"

namespace bot::net {

struct Response {
    long status = 0;          // HTTP status, 0 = transport error
    std::string body;
    std::string error;        // transport error text
};

struct FilePart { std::string field, path, filename; };

// `abort` (optional) lets another thread interrupt a long request (long polling on shutdown).
Response post_json(const std::string& url, const nlohmann::json& body, int timeout_sec = 30,
                   const std::atomic<bool>* abort = nullptr, const std::vector<std::string>& extra_headers = {});
Response post_multipart(const std::string& url, const std::map<std::string, std::string>& fields,
                        const std::vector<FilePart>& files, int timeout_sec = 300);
Response get(const std::string& url, int timeout_sec = 30);
// Streams a URL into `path`. progress(current,total). Returns false on error/cancel.
bool download(const std::string& url, const std::string& path, const std::function<void(i64, i64)>& progress,
              const std::atomic<bool>& cancel, int timeout_sec = 900);

}  // namespace bot::net
