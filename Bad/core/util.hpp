// Small helpers and a process runner.
#pragma once
#include <string>
#include <vector>
#include <optional>
#include "types.hpp"

namespace bot::util {

std::string format_eta(long seconds);
std::string format_size(double bytes);
int to_seconds(const std::string& hhmmss);          // "1:02:03" -> 3723
std::string mmss(long seconds);                      // "%M:%S" of gmtime
std::string html_escape(const std::string& s);
std::string trim(const std::string& s);
std::vector<std::string> split_ws(const std::string& s);
std::string join(const std::vector<std::string>& v, const std::string& sep, size_t from = 0);
bool is_digits(const std::string& s);
bool starts_with(const std::string& s, const std::string& p);
bool contains(const std::vector<std::string>& v, const std::string& x);
std::string lower(std::string s);
std::string uptime_string(long seconds);             // same format as /ping shows
std::string truncate(const std::string& s, size_t n);  // UTF-8 safe (by code points)
std::string url_of(const Message& m);                // get_url()
std::optional<std::string> utf16_substr(const std::string& s, int off, int len);

struct ExecResult { int code = -1; std::string out; std::string err; };   // err = captured stderr (capped)
// fork/exec without a shell: user input can never be interpreted as shell syntax.
ExecResult exec(const std::vector<std::string>& argv, int timeout_sec = 0);

}  // namespace bot::util
