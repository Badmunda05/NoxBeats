#include "cleaner.hpp"
#include <chrono>
#include <condition_variable>
#include <map>
#include <mutex>
#include <thread>
#include "log.hpp"
#include "../tg/client.hpp"

namespace bot {
namespace {

class Cleaner {
public:
    ~Cleaner() {
        { std::lock_guard<std::mutex> lk(mu_); stop_ = true; }
        cv_.notify_all();
        if (th_.joinable()) th_.join();
    }
    void add(i64 chat, i64 mid, int minutes) {
        auto due = std::chrono::steady_clock::now() + std::chrono::minutes(minutes);
        std::lock_guard<std::mutex> lk(mu_);
        items_.emplace(due, std::make_pair(chat, mid));
        if (!started_) { started_ = true; th_ = std::thread([this] { loop(); }); }
        cv_.notify_all();
    }
    void flush() {
        std::multimap<std::chrono::steady_clock::time_point, std::pair<i64, i64>> all;
        { std::lock_guard<std::mutex> lk(mu_); all.swap(items_); }
        for (auto& [_, cm] : all) del(cm.first, cm.second);
    }
private:
    static void del(i64 chat, i64 mid) {
        try { tg::client().delete_messages(chat, {mid}); } catch (...) {}
    }
    void loop() {
        std::unique_lock<std::mutex> lk(mu_);
        while (!stop_) {
            if (items_.empty()) { cv_.wait(lk); continue; }
            auto first = items_.begin();
            if (first->first > std::chrono::steady_clock::now()) { cv_.wait_until(lk, first->first); continue; }
            auto cm = first->second;
            items_.erase(first);
            lk.unlock();
            del(cm.first, cm.second);
            lk.lock();
        }
    }
    std::mutex mu_;
    std::condition_variable cv_;
    std::multimap<std::chrono::steady_clock::time_point, std::pair<i64, i64>> items_;
    std::thread th_;
    bool started_ = false, stop_ = false;
};

Cleaner& cleaner() { static Cleaner c; return c; }

}  // namespace

void schedule_delete(i64 chat, i64 mid, int minutes) {
    if (minutes <= 0 || mid == 0) return;
    cleaner().add(chat, mid, minutes);
}
void cleaner_flush() { cleaner().flush(); }

}  // namespace bot
