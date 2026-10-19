// Minimal thread-safe logger writing to stderr and log.txt (rotates at 10 MB, keeps 5 files).
#pragma once
#include <string>
namespace bot::log {
void info(const std::string& m);
void warn(const std::string& m);
void error(const std::string& m);
}
