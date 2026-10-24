// Clean mode: delete the bot's service messages a few minutes after they were sent.
#pragma once
#include "types.hpp"

namespace bot {
// Deletes message `mid` in `chat` after `minutes` (no-op for minutes <= 0). Thread-safe; a small background thread does the work.
void schedule_delete(i64 chat, i64 mid, int minutes);
// For tests: run every pending deletion now.
void cleaner_flush();
}
