#include "queue.hpp"
#include <algorithm>
#include <random>

namespace bot {

int Queue::add(i64 chat, TrackPtr item) {
    std::lock_guard<std::mutex> lk(mu_);
    auto& d = q_[chat];
    d.push_back(std::move(item));
    return static_cast<int>(d.size()) - 1;
}

std::pair<int, TrackPtr> Queue::check_item(i64 chat, const std::string& id) {
    std::lock_guard<std::mutex> lk(mu_);
    auto& d = q_[chat];
    for (size_t i = 0; i < d.size(); ++i)
        if (d[i]->id == id) return {static_cast<int>(i), d[i]};
    return {-1, nullptr};
}

void Queue::force_add(i64 chat, TrackPtr item, int remove) {
    std::lock_guard<std::mutex> lk(mu_);
    auto& d = q_[chat];
    if (!d.empty()) d.pop_front();           // remove_current
    d.push_front(item);
    if (remove > 0 && static_cast<size_t>(remove) < d.size()) {
        // erase the element at index `remove`
        d.erase(d.begin() + remove);
    } else if (remove == 0) {
        // remove=0 -> nothing extra removed
    }
}

TrackPtr Queue::get_current(i64 chat) {
    std::lock_guard<std::mutex> lk(mu_);
    auto& d = q_[chat];
    return d.empty() ? nullptr : d.front();
}

TrackPtr Queue::get_next(i64 chat, bool check) {
    std::lock_guard<std::mutex> lk(mu_);
    auto& d = q_[chat];
    if (d.empty()) return nullptr;
    if (check) return d.size() > 1 ? d[1] : nullptr;
    d.pop_front();
    return d.empty() ? nullptr : d.front();
}

std::vector<TrackPtr> Queue::get_queue(i64 chat) {
    std::lock_guard<std::mutex> lk(mu_);
    auto& d = q_[chat];
    return {d.begin(), d.end()};
}

void Queue::remove_current(i64 chat) {
    std::lock_guard<std::mutex> lk(mu_);
    auto& d = q_[chat];
    if (!d.empty()) d.pop_front();
}

void Queue::clear(i64 chat) {
    std::lock_guard<std::mutex> lk(mu_);
    q_[chat].clear();
    backup_.erase(chat);
}

size_t Queue::clear_pending(i64 chat) {
    std::lock_guard<std::mutex> lk(mu_);
    auto& d = q_[chat];
    if (d.size() <= 1) return 0;
    backup_[chat].assign(d.begin() + 1, d.end());
    size_t n = d.size() - 1;
    d.erase(d.begin() + 1, d.end());
    return n;
}

bool Queue::restore(i64 chat) {
    std::lock_guard<std::mutex> lk(mu_);
    auto it = backup_.find(chat);
    if (it == backup_.end() || it->second.empty()) return false;
    auto& d = q_[chat];
    for (auto& t : it->second) d.push_back(t);
    backup_.erase(it);
    return true;
}

bool Queue::remove_at(i64 chat, size_t pos, TrackPtr* removed) {
    std::lock_guard<std::mutex> lk(mu_);
    auto& d = q_[chat];
    if (pos < 2 || pos > d.size()) return false;
    if (removed) *removed = d[pos - 1];
    d.erase(d.begin() + (pos - 1));
    return true;
}

bool Queue::move(i64 chat, size_t from, size_t to) {
    std::lock_guard<std::mutex> lk(mu_);
    auto& d = q_[chat];
    if (from < 2 || to < 2 || from > d.size() || to > d.size()) return false;
    TrackPtr t = d[from - 1];
    d.erase(d.begin() + (from - 1));
    d.insert(d.begin() + (to - 1), t);
    return true;
}

size_t Queue::shuffle_pending(i64 chat) {
    std::lock_guard<std::mutex> lk(mu_);
    auto& d = q_[chat];
    if (d.size() <= 2) return d.size() > 1 ? 1 : 0;
    static std::mt19937 rng{std::random_device{}()};
    std::shuffle(d.begin() + 1, d.end(), rng);
    return d.size() - 1;
}

Queue& queue() {
    static Queue q;
    return q;
}

}  // namespace bot
