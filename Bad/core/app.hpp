#pragma once
namespace bot {
// Ask main() to shut down; restart=true re-execs the binary afterwards.
void request_stop(bool restart = false);
bool restart_requested();
namespace tg { class Client; }
void set_app_client(tg::Client* c);
extern const long g_boot_time;     // seconds since epoch at start
}
