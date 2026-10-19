#include "queue.hpp"

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
}

Queue& queue() {
    static Queue q;
    return q;
}

}  // namespace bot
