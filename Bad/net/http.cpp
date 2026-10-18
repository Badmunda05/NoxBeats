#include "http.hpp"
#include <curl/curl.h>
#include <cstdio>
#include <mutex>

namespace bot::net {
namespace {

struct CurlGlobal {
    CurlGlobal() { curl_global_init(CURL_GLOBAL_DEFAULT); }
    ~CurlGlobal() { curl_global_cleanup(); }
};

CURL* handle() {
    static CurlGlobal g;
    struct Holder { CURL* h = curl_easy_init(); ~Holder() { if (h) curl_easy_cleanup(h); } };
    thread_local Holder holder;
    curl_easy_reset(holder.h);
    return holder.h;
}

size_t write_str(char* p, size_t s, size_t n, void* ud) {
    static_cast<std::string*>(ud)->append(p, s * n);
    return s * n;
}

struct Ctx {
    const std::atomic<bool>* abort = nullptr;
    std::function<void(i64, i64)> progress;
};

int xfer(void* ud, curl_off_t dltotal, curl_off_t dlnow, curl_off_t, curl_off_t) {
    auto* c = static_cast<Ctx*>(ud);
    if (c->abort && c->abort->load()) return 1;
    if (c->progress) c->progress(dlnow, dltotal);
    return 0;
}

Response finish(CURL* h, CURLcode rc, std::string&& body, char* errbuf) {
    Response r;
    r.body = std::move(body);
    if (rc != CURLE_OK) {
        r.error = errbuf[0] ? errbuf : curl_easy_strerror(rc);
        return r;
    }
    curl_easy_getinfo(h, CURLINFO_RESPONSE_CODE, &r.status);
    return r;
}

}  // namespace

Response post_json(const std::string& url, const nlohmann::json& body, int timeout, const std::atomic<bool>* abort,
                   const std::vector<std::string>& extra_headers) {
    CURL* h = handle();
    std::string out, payload = body.dump(), err(CURL_ERROR_SIZE, '\0');
    curl_slist* hdr = curl_slist_append(nullptr, "Content-Type: application/json");
    for (const auto& eh : extra_headers) hdr = curl_slist_append(hdr, eh.c_str());
    Ctx ctx{abort, {}};
    curl_easy_setopt(h, CURLOPT_URL, url.c_str());
    curl_easy_setopt(h, CURLOPT_POST, 1L);
    curl_easy_setopt(h, CURLOPT_POSTFIELDS, payload.c_str());
    curl_easy_setopt(h, CURLOPT_POSTFIELDSIZE, static_cast<long>(payload.size()));
    curl_easy_setopt(h, CURLOPT_HTTPHEADER, hdr);
    curl_easy_setopt(h, CURLOPT_WRITEFUNCTION, write_str);
    curl_easy_setopt(h, CURLOPT_WRITEDATA, &out);
    curl_easy_setopt(h, CURLOPT_TIMEOUT, static_cast<long>(timeout));
    curl_easy_setopt(h, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(h, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(h, CURLOPT_ERRORBUFFER, err.data());
    curl_easy_setopt(h, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(h, CURLOPT_XFERINFOFUNCTION, xfer);
    curl_easy_setopt(h, CURLOPT_XFERINFODATA, &ctx);
    CURLcode rc = curl_easy_perform(h);
    Response r = finish(h, rc, std::move(out), err.data());
    curl_slist_free_all(hdr);
    return r;
}

Response post_multipart(const std::string& url, const std::map<std::string, std::string>& fields,
                        const std::vector<FilePart>& files, int timeout) {
    CURL* h = handle();
    std::string out, err(CURL_ERROR_SIZE, '\0');
    curl_mime* mime = curl_mime_init(h);
    for (const auto& [k, v] : fields) {
        curl_mimepart* p = curl_mime_addpart(mime);
        curl_mime_name(p, k.c_str());
        curl_mime_data(p, v.c_str(), CURL_ZERO_TERMINATED);
    }
    for (const auto& f : files) {
        curl_mimepart* p = curl_mime_addpart(mime);
        curl_mime_name(p, f.field.c_str());
        curl_mime_filedata(p, f.path.c_str());
        if (!f.filename.empty()) curl_mime_filename(p, f.filename.c_str());
    }
    curl_easy_setopt(h, CURLOPT_URL, url.c_str());
    curl_easy_setopt(h, CURLOPT_MIMEPOST, mime);
    curl_easy_setopt(h, CURLOPT_WRITEFUNCTION, write_str);
    curl_easy_setopt(h, CURLOPT_WRITEDATA, &out);
    curl_easy_setopt(h, CURLOPT_TIMEOUT, static_cast<long>(timeout));
    curl_easy_setopt(h, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(h, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(h, CURLOPT_ERRORBUFFER, err.data());
    CURLcode rc = curl_easy_perform(h);
    Response r = finish(h, rc, std::move(out), err.data());
    curl_mime_free(mime);
    return r;
}

Response get(const std::string& url, int timeout) {
    CURL* h = handle();
    std::string out, err(CURL_ERROR_SIZE, '\0');
    curl_easy_setopt(h, CURLOPT_URL, url.c_str());
    curl_easy_setopt(h, CURLOPT_WRITEFUNCTION, write_str);
    curl_easy_setopt(h, CURLOPT_WRITEDATA, &out);
    curl_easy_setopt(h, CURLOPT_TIMEOUT, static_cast<long>(timeout));
    curl_easy_setopt(h, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(h, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(h, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(h, CURLOPT_ERRORBUFFER, err.data());
    CURLcode rc = curl_easy_perform(h);
    return finish(h, rc, std::move(out), err.data());
}

bool download(const std::string& url, const std::string& path, const std::function<void(i64, i64)>& progress,
              const std::atomic<bool>& cancel, int timeout) {
    CURL* h = handle();
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) return false;
    std::string err(CURL_ERROR_SIZE, '\0');
    Ctx ctx{&cancel, progress};
    curl_easy_setopt(h, CURLOPT_URL, url.c_str());
    curl_easy_setopt(h, CURLOPT_WRITEFUNCTION, nullptr);   // default fwrite
    curl_easy_setopt(h, CURLOPT_WRITEDATA, f);
    curl_easy_setopt(h, CURLOPT_TIMEOUT, static_cast<long>(timeout));
    curl_easy_setopt(h, CURLOPT_CONNECTTIMEOUT, 15L);
    curl_easy_setopt(h, CURLOPT_FAILONERROR, 1L);
    curl_easy_setopt(h, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(h, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(h, CURLOPT_ERRORBUFFER, err.data());
    curl_easy_setopt(h, CURLOPT_NOPROGRESS, 0L);
    curl_easy_setopt(h, CURLOPT_XFERINFOFUNCTION, xfer);
    curl_easy_setopt(h, CURLOPT_XFERINFODATA, &ctx);
    CURLcode rc = curl_easy_perform(h);
    std::fclose(f);
    if (rc != CURLE_OK) {
        std::fprintf(stderr, "download failed: %s\n", err[0] ? err.c_str() : curl_easy_strerror(rc));
        std::remove(path.c_str());
        return false;
    }
    return true;
}

}  // namespace bot::net
