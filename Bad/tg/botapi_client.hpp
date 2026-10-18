// Bot account over the Telegram Bot API (HTTPS long polling). Assistants (userbots) are handled by the
// assistant accounts: join/leave of chats go through AssistantManager (in-process MTProto).
#pragma once
#include <atomic>
#include <deque>
#include <map>
#include <mutex>
#include "client.hpp"
#include "../voice/assistant.hpp"

namespace bot::tg {

class BotApiClient : public Client {
public:
    // base_url: "https://api.telegram.org" or a local Bot API server (allows 2 GB downloads).
    BotApiClient(std::string token, std::string base_url, voice::AssistantManager* assistants);
    bool init(std::string* err);        // getMe

    User me() override { return me_; }
    int assistants() override { return a_ ? a_->count() : 0; }
    User assistant(int idx) override { return a_->user(idx); }

    Message send_message(i64 chat, const std::string& html, const Keyboard& kb, i64 reply_to) override;
    Message send_photo(i64 chat, const std::string& photo, const std::string& caption, const Keyboard& kb, i64 reply_to) override;
    void send_document(i64 chat, const std::string& path, const std::string& caption) override;
    void edit_text(i64 chat, i64 mid, const std::string& html, const Keyboard& kb) override;
    void edit_caption(i64 chat, i64 mid, const std::string& html, const Keyboard& kb) override;
    void edit_photo(i64 chat, i64 mid, const std::string& photo, const std::string& caption, const Keyboard& kb) override;
    void edit_markup(i64 chat, i64 mid, const Keyboard& kb) override;
    void delete_messages(i64 chat, const std::vector<i64>& ids) override;
    void answer_callback(const std::string& id, const std::string& text, bool alert) override;
    void answer_callback_url(const std::string& id, const std::string& url) override;

    MemberStatus member_status(i64 chat, i64 user) override;
    std::vector<i64> admins(i64 chat) override;
    void unban(i64 chat, i64 user) override;
    std::string invite_link(i64 chat) override;
    void approve_join_request(i64 chat, i64 user) override;
    void assistant_join(int idx, const std::string& link_or_username) override;
    void assistant_leave(int idx, i64 chat) override;
    bool assistant_send(int idx, i64 chat, const std::string& text) override;
    std::vector<i64> assistant_groups(int idx) override;
    void leave_chat(i64 chat) override;

    std::optional<User> get_user(const std::string& username) override;
    std::optional<User> get_user(i64 id) override;
    std::optional<Chat> get_chat(const std::string& username) override;

    void copy_message(i64 to, const Message& m) override;
    void forward_message(i64 to, const Message& m) override;
    i64 max_download() override { return local_server_ ? 2000LL * 1024 * 1024 : 20LL * 1024 * 1024; }
    bool download_media(const Message& m, const std::string& path, const std::function<void(Progress)>& progress,
                        const std::atomic<bool>& cancel) override;

    void run() override;
    void stop() override;

    // exposed for tests
    nlohmann::json call(const std::string& method, const nlohmann::json& params);
    static Message parse_message(const nlohmann::json& j);
    static nlohmann::json keyboard_json(const Keyboard& kb);

private:
    void handle_update(const nlohmann::json& u);
    Message sent_message(const nlohmann::json& result, const std::string& html);
    void remember(i64 chat, i64 mid, const std::string& html);
    std::string html_of(i64 chat, i64 mid);
    nlohmann::json edit_common(i64 chat, i64 mid, const std::string& method, const std::string& field,
                               const std::string& html, const Keyboard& kb);

    std::string token_, base_;
    bool local_server_ = false;
    voice::AssistantManager* a_;
    User me_;
    std::atomic<bool> stop_{false};

    std::mutex cache_mu_;
    std::map<std::pair<i64, i64>, std::string> html_cache_;
    std::deque<std::pair<i64, i64>> html_order_;
};

}  // namespace bot::tg
