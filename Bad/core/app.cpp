#include "app.hpp"
#include <atomic>
#include <ctime>
#include "../tg/client.hpp"

namespace bot {
const long g_boot_time = static_cast<long>(std::time(nullptr));
static std::atomic<bool> g_restart{false};
static std::atomic<tg::Client*> g_client{nullptr};
void set_app_client(tg::Client* c) { g_client = c; }
void request_stop(bool restart) {
    g_restart = restart;
    if (auto* c = g_client.load()) c->stop();
}
bool restart_requested() { return g_restart; }
}  // namespace bot
