// Command router + shared decorators (port of lang.language(), admin_check, can_manage_vc, utilities).
#pragma once
#include <functional>
#include <set>
#include <string>
#include <vector>
#include "../core/types.hpp"
#include "../i18n/lang.hpp"

namespace bot {

enum class Scope { Any, Group, Private };
enum class Perm { Everyone, Sudo, Owner };

using MsgHandler = std::function<void(const Message&, const Strings&)>;
using CbHandler = std::function<void(const CallbackQuery&, const Strings&)>;

void on_command(std::vector<std::string> cmds, Scope scope, Perm perm, MsgHandler fn);
void on_callback(const std::string& regex, CbHandler fn);
void on_new_members(MsgHandler fn);
void on_video_chat(MsgHandler fn);     // started or ended

// Entry points called by the Telegram client.
void dispatch_message(const Message& m);
void dispatch_callback(const CallbackQuery& q);
size_t module_count();                 // number of registered handler groups (for /stats)
void register_module(const std::string& name);

// --- identity / permissions ---
bool is_sudo(i64 id);
bool is_bl_user(i64 id);
void add_bl_user(i64 id);
void rm_bl_user(i64 id);
size_t sudo_count();
void load_runtime_sets();              // sudoers + blacklisted users from DB

bool is_admin(i64 chat, i64 user);
// Both return false after sending a "no permission" reply.
bool admin_check(const Message& m, const Strings& L);
bool admin_check(const CallbackQuery& q, const Strings& L);
bool can_manage_vc(const Message& m, const Strings& L);
bool can_manage_vc(const CallbackQuery& q, const Strings& L);

// --- helpers ---
std::optional<User> extract_user(const Message& m);
void play_log(const Message& m, const std::string& link, const std::string& title, const std::string& duration, const Strings& L);
void send_log(const Message& m, const Strings& L, bool chat = false);
// The bot was removed from / blocked in a chat: tidy up and tell the log chat.
void handle_bot_member(const BotMemberChange& c);
std::string msg_link(i64 chat, i64 mid);
void sudo_add(i64 id);
void sudo_del(i64 id);
// Makes sure the assistant account is in the chat that hosts the voice chat (`target`, 0 = this chat); false after replying.
bool ensure_assistant(const Message& m, const Strings& L, i64 target = 0);
// Reply helper: sends a reply and swallows nothing (callers are wrapped by the router).
Message reply(const Message& m, const std::string& text, const Keyboard& kb = {});

}  // namespace bot
