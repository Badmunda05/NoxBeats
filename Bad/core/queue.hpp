// Chat queues. Thread-safe; items are shared so callers can mutate .time / .message_id.
#pragma once
#include <deque>
#include <map>
#include <mutex>
#include <utility>
#include <vector>
#include "types.hpp"

namespace bot {

class Queue {
public:
    // Appends and returns the 0-based position (0 == currently playing).
    int add(i64 chat, TrackPtr item);
    // Finds by id: (position, item) or (-1, nullptr).
    std::pair<int, TrackPtr> check_item(i64 chat, const std::string& id);
    // Replaces the current item. `remove` >= 0 also removes the entry that used to be at that position.
    void force_add(i64 chat, TrackPtr item, int remove = -1);
    TrackPtr get_current(i64 chat);
    // check=false: pop current, return the new current. check=true: peek the next one.
    TrackPtr get_next(i64 chat, bool check = false);
    std::vector<TrackPtr> get_queue(i64 chat);
    void remove_current(i64 chat);
    void clear(i64 chat);
private:
    std::mutex mu_;
    std::map<i64, std::deque<TrackPtr>> q_;
};

Queue& queue();

}  // namespace bot
