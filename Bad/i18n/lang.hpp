// JSON string tables with "{0}" / "{2:.1f}" formatting.
#pragma once
#include <map>
#include <string>
#include <vector>
#include <variant>
#include "../core/types.hpp"

namespace bot {

struct Arg {
    std::string s;
    bool numeric = false;
    double d = 0;
    Arg(const char* v) : s(v) {}
    Arg(const std::string& v) : s(v) {}
    Arg(bool v) : s(v ? "True" : "False") {}
    Arg(int v) : s(std::to_string(v)), numeric(true), d(v) {}
    Arg(long v) : s(std::to_string(v)), numeric(true), d(static_cast<double>(v)) {}
    Arg(long long v) : s(std::to_string(v)), numeric(true), d(static_cast<double>(v)) {}
    Arg(unsigned long v) : s(std::to_string(v)), numeric(true), d(static_cast<double>(v)) {}
    Arg(double v);
};

std::string fmt(const std::string& tpl, const std::vector<Arg>& args);

class Strings {
public:
    explicit Strings(std::map<std::string, std::string> t = {}) : t_(std::move(t)) {}
    // Missing keys fall back to the key itself so a broken locale never crashes a handler.
    const std::string& operator[](const std::string& k) const;
    std::string f(const std::string& k, const std::vector<Arg>& a) const { return fmt((*this)[k], a); }
private:
    std::map<std::string, std::string> t_;
    mutable std::map<std::string, std::string> miss_;
};

class Language {
public:
    explicit Language(const std::string& dir = "strings");
    const Strings& get(const std::string& code) const;       // falls back to "en"
    const std::map<std::string, std::string>& codes() const { return codes_; }
    std::map<std::string, std::string> available() const;     // code -> native name (sorted)
    bool has(const std::string& code) const { return langs_.count(code) > 0; }
private:
    std::map<std::string, Strings> langs_;
    std::map<std::string, std::string> codes_;
};

Language& lang();   // global
void init_language(const std::string& dir = "strings");

}  // namespace bot
