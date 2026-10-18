// Tiny fixed-size thread pool: every incoming update is handled on a worker (python used asyncio tasks).
#pragma once
#include <condition_variable>
#include <functional>
#include <mutex>
#include <queue>
#include <thread>
#include <vector>

namespace bot {
class ThreadPool {
public:
    explicit ThreadPool(size_t n = 16) {
        for (size_t i = 0; i < n; ++i) workers_.emplace_back([this] { loop(); });
    }
    ~ThreadPool() {
        { std::lock_guard<std::mutex> lk(mu_); stop_ = true; }
        cv_.notify_all();
        for (auto& t : workers_) t.join();
    }
    void submit(std::function<void()> f) {
        { std::lock_guard<std::mutex> lk(mu_); q_.push(std::move(f)); }
        cv_.notify_one();
    }
    // Wait until the queue is empty and workers are idle (used by tests).
    void drain() {
        std::unique_lock<std::mutex> lk(mu_);
        idle_cv_.wait(lk, [this] { return q_.empty() && busy_ == 0; });
    }
private:
    void loop() {
        for (;;) {
            std::function<void()> f;
            {
                std::unique_lock<std::mutex> lk(mu_);
                cv_.wait(lk, [this] { return stop_ || !q_.empty(); });
                if (stop_ && q_.empty()) return;
                f = std::move(q_.front());
                q_.pop();
                ++busy_;
            }
            try { f(); } catch (...) {}
            { std::lock_guard<std::mutex> lk(mu_); --busy_; }
            idle_cv_.notify_all();
        }
    }
    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> q_;
    std::mutex mu_;
    std::condition_variable cv_, idle_cv_;
    size_t busy_ = 0;
    bool stop_ = false;
};
}  // namespace bot
