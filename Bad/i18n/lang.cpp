#include "lang.hpp"
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <memory>
#include <nlohmann/json.hpp>
#include "../core/log.hpp"

namespace bot {

Arg::Arg(double v) : numeric(true), d(v) {
    char b[64];
    std::snprintf(b, sizeof b, "%g", v);
    s = b;
}

std::string fmt(const std::string& t, const std::vector<Arg>& args) {
    std::string out;
    out.reserve(t.size() + 32);
    for (size_t i = 0; i < t.size(); ++i) {
        if (t[i] == '{') {
            size_t close = t.find('}', i);
            if (close != std::string::npos) {
                std::string spec = t.substr(i + 1, close - i - 1);
                std::string idx = spec, fs;
                if (auto c = spec.find(':'); c != std::string::npos) { idx = spec.substr(0, c); fs = spec.substr(c + 1); }
                bool digits = !idx.empty() && idx.find_first_not_of("0123456789") == std::string::npos;
                if (digits) {
                    size_t n = std::stoul(idx);
                    if (n < args.size()) {
                        const Arg& a = args[n];
                        if (fs.size() >= 3 && fs[0] == '.' && fs.back() == 'f' && a.numeric) {
                            char b[64];
                            std::snprintf(b, sizeof b, ("%" + fs).c_str(), a.d);
                            out += b;
                        } else {
                            out += a.s;
                        }
                    }
                    i = close;
                    continue;
                }
            }
        }
        out += t[i];
    }
    return out;
}

const std::string& Strings::operator[](const std::string& k) const {
    auto it = t_.find(k);
    if (it != t_.end()) return it->second;
    return miss_.emplace(k, k).first->second;
}

Language::Language(const std::string& dir) {
    codes_ = {{"ar", "العربية"}, {"de", "Deutsch"}, {"en", "English"}, {"es", "Español"},
              {"fr", "Français"}, {"hi", "हिन्दी"}, {"ja", "日本語"}, {"my", "မြန်မာဘာသာ"},
              {"pa", "ਪੰਜਾਬੀ"}, {"pt", "Português"}, {"ru", "Русский"}, {"tr", "Türkçe"}, {"zh", "中文"}};
    namespace fs = std::filesystem;
    std::string names;
    for (const auto& e : fs::directory_iterator(dir)) {
        if (e.path().extension() != ".json") continue;
        try {
            std::ifstream f(e.path());
            nlohmann::json j = nlohmann::json::parse(f);
            std::map<std::string, std::string> m;
            for (auto it = j.begin(); it != j.end(); ++it)
                if (it.value().is_string()) m[it.key()] = it.value().get<std::string>();
            std::string code = e.path().stem().string();
            langs_.emplace(code, Strings(std::move(m)));
            names += (names.empty() ? "" : ", ") + code;
        } catch (const std::exception& ex) {
            log::warn("Failed to load " + e.path().string() + ": " + ex.what());
        }
    }
    if (!langs_.count("en")) throw std::runtime_error("strings/en.json is required");
    log::info("Loaded languages: " + names);
}

const Strings& Language::get(const std::string& code) const {
    auto it = langs_.find(code);
    return it != langs_.end() ? it->second : langs_.at("en");
}

std::map<std::string, std::string> Language::available() const {
    std::map<std::string, std::string> r;
    for (const auto& [code, _] : langs_) {
        auto it = codes_.find(code);
        r[code] = it != codes_.end() ? it->second : code;
    }
    return r;
}

static std::unique_ptr<Language> g_lang;
Language& lang() { return *g_lang; }
void init_language(const std::string& dir) { g_lang = std::make_unique<Language>(dir); }

}  // namespace bot
