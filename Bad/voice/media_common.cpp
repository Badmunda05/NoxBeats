#include "media.hpp"
#include <cstring>

namespace bot::voice {

std::string shell_quote(const std::string& s) {
    std::string o = "'";
    for (char c : s) {
        if (c == '\'') o += "'\\''";
        else o += c;
    }
    return o + "'";
}

std::string build_ffmpeg_cmd(const std::string& src, int seek) {
    if (src.empty() || src.size() > 4096 || src[0] == '-') throw std::invalid_argument("bad media source");
    for (unsigned char c : src)
        if (c < 0x20 || c == 0x7f) throw std::invalid_argument("bad media source");
    std::string cmd = "ffmpeg ";
    bool net = src.rfind("http://", 0) == 0 || src.rfind("https://", 0) == 0;
    if (net) cmd += "-re -reconnect 1 -reconnect_streamed 1 -reconnect_delay_max 5 ";
    if (seek > 0) cmd += "-ss " + std::to_string(seek) + " ";
    cmd += "-v warning -i " + shell_quote(src) + " -f s16le -ac 2 -ar 96000 pipe:1";
    return cmd;
}

}  // namespace bot::voice
