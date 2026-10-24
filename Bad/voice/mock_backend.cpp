#include "mock_backend.hpp"
#include <filesystem>
namespace bot::voice {
void MockBackend::play(int idx, i64 chat, const std::string& path, bool video, int seek, double speed, i64 peer, int bass) {
    if (!has_vc) throw Error(Err::NoActiveGroupCall, "no vc");
    bool remote = path.rfind("http", 0) == 0;
    if (!remote && !std::filesystem::exists(path)) throw Error(Err::FileNotFound, path);
    rec({"play", idx, chat, path, video, seek, speed, peer, bass});
}
}
