#include "log.hpp"
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <mutex>

namespace bot::log {
namespace {
std::mutex mu;
void rotate_if_needed() {
    namespace fs = std::filesystem;
    std::error_code ec;
    if (!fs::exists("log.txt", ec) || fs::file_size("log.txt", ec) < 10485760) return;
    for (int i = 4; i >= 1; --i) {
        std::string from = "log.txt." + std::to_string(i), to = "log.txt." + std::to_string(i + 1);
        if (fs::exists(from, ec)) fs::rename(from, to, ec);
    }
    fs::rename("log.txt", "log.txt.1", ec);
}
void write(const char* level, const std::string& m) {
    char ts[32];
    std::time_t t = std::time(nullptr);
    std::tm tm{};
    gmtime_r(&t, &tm);
    std::strftime(ts, sizeof ts, "%d-%b-%y %H:%M:%S", &tm);
    std::string line = std::string("[") + ts + " - " + level + "] " + m + "\n";
    std::lock_guard<std::mutex> lk(mu);
    std::cerr << line;
    rotate_if_needed();
    std::ofstream("log.txt", std::ios::app) << line;
}
}  // namespace
void info(const std::string& m) { write("INFO", m); }
void warn(const std::string& m) { write("WARNING", m); }
void error(const std::string& m) { write("ERROR", m); }
}  // namespace bot::log
