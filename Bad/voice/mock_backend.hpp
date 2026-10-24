#pragma once
#include <mutex>
#include <set>
#include <vector>
#include "backend.hpp"

namespace bot::voice {
class MockBackend : public Backend {
public:
    struct Call { std::string op; int idx; i64 chat; std::string path; bool video; int seek; double speed = 1.0; i64 peer = 0; int bass = 0; };
    std::vector<Call> calls;
    std::mutex mu;
    int participants_n = 2;
    bool has_vc = true;                      // false => NoActiveGroupCall

    void play(int idx, i64 chat, const std::string& path, bool video, int seek, double speed = 1.0, i64 peer = 0, int bass = 0) override;
    void mute(int idx, i64 chat) override { rec({"mute", idx, chat, "", false, 0}); }
    void unmute(int idx, i64 chat) override { rec({"unmute", idx, chat, "", false, 0}); }
    void pause(int idx, i64 chat) override { rec({"pause", idx, chat, "", false, 0}); }
    void resume(int idx, i64 chat) override { rec({"resume", idx, chat, "", false, 0}); }
    void leave(int idx, i64 chat) override { rec({"leave", idx, chat, "", false, 0}); }
    int participants(int, i64) override { return participants_n; }
    double ping() override { return 12.5; }
    size_t count(const std::string& op) { std::lock_guard<std::mutex> lk(mu); size_t n = 0; for (auto& c : calls) n += c.op == op; return n; }
private:
    void rec(Call c) { std::lock_guard<std::mutex> lk(mu); calls.push_back(std::move(c)); }
};
}
