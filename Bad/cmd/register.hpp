// Plugin registration (python: bad/plugins/__init__.py + importlib in __main__).
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
void start_background_tasks();
void stop_background_tasks();
inline void register_all() {
    register_play(); register_playback(); register_play_cb(); register_admin();
    register_sudo(); register_broadcast(); register_settings(); register_tools();
}
}
