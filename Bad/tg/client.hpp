// Telegram abstraction. The bot logic only talks to this interface; implement it with TDLib
// (Bad/tg/tdlib_client.cpp in your tree) or use MockClient for tests.
// Replaces pyrogram.Client (bot account) + userbot join/leave helpers.
#pragma once
#include <atomic>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>
#include "../core/types.hpp"

namespace bot::tg {

enum class Err {
    FloodWait, SlowmodeWait, ChatAdminRequired, UserNotParticipant, UserAlreadyParticipant,
    InviteRequestSent, HideRequesterMissing, Forbidden, ChatWriteForbidden, ChatSendMediaForbidden,
    MessageNotModified, MessageIdInvalid, ChannelInvalid, ChannelPrivate, QueryIdInvalid, Other
};

struct Error : std::runtime_error {
    Err kind;
    int wait = 0;
    Error(Err k, const std::string& what, int w = 0) : std::runtime_error(what), kind(k), wait(w) {}
};

enum class MemberStatus { Owner, Admin, Member, Restricted, Banned, NotMember };

struct Progress { i64 current = 0, total = 0; };

class Client {
public:
    virtual ~Client() = default;

    virtual User me() = 0;                                    // the bot account
    virtual int assistants() = 0;                             // number of userbot accounts
    virtual User assistant(int idx) = 0;                      // 1-based

    virtual Message send_message(i64 chat, const std::string& html, const Keyboard& kb = {}, i64 reply_to = 0) = 0;
    virtual Message send_photo(i64 chat, const std::string& photo, const std::string& caption,
                               const Keyboard& kb = {}, i64 reply_to = 0) = 0;
    virtual void send_document(i64 chat, const std::string& path, const std::string& caption) = 0;
    virtual void edit_text(i64 chat, i64 mid, const std::string& html, const Keyboard& kb = {}) = 0;
    virtual void edit_caption(i64 chat, i64 mid, const std::string& html, const Keyboard& kb = {}) = 0;
    // Replace the message content with a photo (used for "Started streaming" + thumbnail).
    virtual void edit_photo(i64 chat, i64 mid, const std::string& photo, const std::string& caption,
                            const Keyboard& kb = {}) = 0;
    virtual void edit_markup(i64 chat, i64 mid, const Keyboard& kb) = 0;
    virtual void delete_messages(i64 chat, const std::vector<i64>& ids) = 0;
    virtual void answer_callback(const std::string& id, const std::string& text = "", bool alert = false) = 0;
    virtual void answer_callback_url(const std::string& id, const std::string& url) = 0;

    virtual MemberStatus member_status(i64 chat, i64 user) = 0;   // bot-side lookup; throws Error
    virtual std::vector<i64> admins(i64 chat) = 0;                // non-bot administrators
    virtual void unban(i64 chat, i64 user) = 0;
    virtual std::string invite_link(i64 chat) = 0;                // existing or newly exported
    virtual void approve_join_request(i64 chat, i64 user) = 0;
    virtual void assistant_join(int idx, const std::string& link_or_username) = 0;
    virtual void assistant_leave(int idx, i64 chat) = 0;
    virtual bool assistant_send(int idx, i64 chat, const std::string& text) = 0;   // message sent by the assistant account
    virtual std::vector<i64> assistant_groups(int idx) = 0;       // for auto_leave
    virtual void leave_chat(i64 chat) = 0;

    virtual std::optional<User> get_user(const std::string& username) = 0;   // "@name"
    virtual std::optional<User> get_user(i64 id) = 0;
    virtual std::optional<Chat> get_chat(const std::string& username) = 0;

    virtual void copy_message(i64 to, const Message& m) = 0;
    virtual void forward_message(i64 to, const Message& m) = 0;

    // Largest file download_media() can fetch (Bot API: 20 MB, local Bot API server: 2 GB).
    virtual i64 max_download() { return 200LL * 1024 * 1024; }

    // Download a media message to `path`. Return false if cancelled/failed.
    virtual bool download_media(const Message& m, const std::string& path,
                                const std::function<void(Progress)>& progress,
                                const std::atomic<bool>& cancel) = 0;

    // --- update loop ---
    std::function<void(const Message&)> on_message;
    std::function<void(const CallbackQuery&)> on_callback;
    virtual void run() = 0;        // blocks until stop()
    virtual void stop() = 0;
};

Client& client();                  // global bot client
void set_client(Client* c);

}  // namespace bot::tg
