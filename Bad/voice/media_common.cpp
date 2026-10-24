#include "media.hpp"
#include <cstdio>
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

static std::string ffmpeg_input(const std::string& src, int seek) {
    if (src.empty() || src.size() > 4096 || src[0] == '-') throw std::invalid_argument("bad media source");
    for (unsigned char c : src)
        if (c < 0x20 || c == 0x7f) throw std::invalid_argument("bad media source");
    std::string cmd = "ffmpeg ";
    bool net = src.rfind("http://", 0) == 0 || src.rfind("https://", 0) == 0;
    if (net) cmd += "-re -reconnect 1 -reconnect_streamed 1 -reconnect_delay_max 5 ";
    if (seek > 0) cmd += "-ss " + std::to_string(seek) + " ";
    return cmd + "-v warning -i " + shell_quote(src);
}

static double clamp_speed(double s) { return s < 0.5 ? 0.5 : s > 2.0 ? 2.0 : s; }
static std::string num(double v) { char b[32]; std::snprintf(b, sizeof b, "%.3f", v); return b; }

std::string build_ffmpeg_cmd(const std::string& src, int seek, double speed, int bass) {
    std::string cmd = ffmpeg_input(src, seek) + " -vn";
    speed = clamp_speed(speed);
    bass = bass < 0 ? 0 : bass > 100 ? 100 : bass;
    std::string af;
    if (speed != 1.0) af = "atempo=" + num(speed);
    if (bass > 0) {
        // level 1 is already clearly audible (+6 dB low shelf), level 100 is +24 dB; a second peak at 55 Hz adds the sub-bass
        // and the limiter keeps the boosted signal from clipping
        const double g = 4.0 + bass * 0.2;
        if (!af.empty()) af += ",";
        af += "bass=g=" + num(g) + ":f=100:w=0.8,equalizer=f=55:t=o:w=1.5:g=" + num(g / 2) + ",alimiter=limit=0.95";
    }
    if (!af.empty()) cmd += " -af " + af;
    return cmd + " -f s16le -ac 2 -ar 96000 pipe:1";
}

std::string build_ffmpeg_video_cmd(const std::string& src, int seek, double speed) {
    const std::string w = std::to_string(kVideoWidth), h = std::to_string(kVideoHeight);
    speed = clamp_speed(speed);
    const std::string pts = speed != 1.0 ? "setpts=PTS/" + num(speed) + "," : "";
    return ffmpeg_input(src, seek) + " -an -map 0:v:0 -vf \"" + pts + "scale=" + w + ":" + h + ":force_original_aspect_ratio=decrease,pad=" + w + ":" + h +
           ":(ow-iw)/2:(oh-ih)/2,format=yuv420p\" -r " + std::to_string(kVideoFps) + " -f rawvideo pipe:1";
}

}  // namespace bot::voice
