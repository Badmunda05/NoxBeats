// Minimal thread-safe logger writing to stderr and log.txt (python: RotatingFileHandler 10MB x5).
#pragma once
#include <string>
namespace bot::log {
void info(const std::string& m);
void warn(const std::string& m);
void error(const std::string& m);
}
