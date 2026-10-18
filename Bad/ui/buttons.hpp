// Port of bad/helpers/_inline.py
#pragma once
#include <string>
#include "../core/types.hpp"
#include "../i18n/lang.hpp"

namespace bot::buttons {
Keyboard cancel_dl(const std::string& text);
Keyboard controls(i64 chat, const std::string& status = "", const std::string& timer = "", bool remove = false);
Keyboard help_markup(const Strings& L, bool back = false);
Keyboard lang_markup(const std::string& current);
Keyboard ping_markup(const std::string& text);
Keyboard play_queued(i64 chat, const std::string& item_id, const std::string& text);
Keyboard queue_markup(i64 chat, const std::string& text, bool playing);
Keyboard settings_markup(const Strings& L, bool admin_only, bool cmd_delete, const std::string& language);
Keyboard start_key(const Strings& L, bool is_private);
Keyboard yt_key(const std::string& link);
}
