// Command module registration.
#pragma once
namespace bot {
void register_play();
void register_playback();
void register_play_cb();
void register_admin();
void register_sudo();
void register_broadcast();
void register_settings();
void register_tools();
void register_autoplay();
void register_queue_ops();
void register_chat_settings();
void register_bot_tools();
void register_cplay();
void register_rtmp();
void register_playlist();
void start_background_tasks();
void stop_background_tasks();
inline void register_all() {
    register_play(); register_playback(); register_play_cb(); register_admin();
    register_sudo(); register_broadcast(); register_settings(); register_tools(); register_autoplay(); register_queue_ops();
    register_chat_settings(); register_bot_tools(); register_cplay(); register_rtmp(); register_playlist();
}
}
