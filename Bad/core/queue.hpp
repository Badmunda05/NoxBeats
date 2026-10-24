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
    // Queue editing. Positions are the numbers /queue shows: 1 = the playing track, 2 = the first one waiting, ...
    // clear_pending keeps what it removed so restore() can bring it back.
    size_t clear_pending(i64 chat);
    bool restore(i64 chat);
    bool remove_at(i64 chat, size_t pos, TrackPtr* removed = nullptr);
    bool move(i64 chat, size_t from, size_t to);
    size_t shuffle_pending(i64 chat);
private:
    std::mutex mu_;
    std::map<i64, std::deque<TrackPtr>> q_, backup_;
};

Queue& queue();

}  // namespace bot
