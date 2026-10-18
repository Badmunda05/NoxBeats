// In-memory Telegram client for tests and for running the bot logic without TDLib.
#pragma once
#include <condition_variable>
#include <map>
#include <mutex>
#include <set>
#include "client.hpp"

namespace bot::tg {

class MockClient : public Client {
public:
    struct Event { std::string kind; i64 chat = 0; i64 mid = 0; std::string text; std::string extra; Keyboard kb; };

    User bot_user{1000, "TestBot", "testbot", true};
    std::vector<User> assistant_users{{2001, "Assistant1", "ub1", false}, {2002, "Assistant2", "ub2", false}};
    std::map<i64, std::set<i64>> admin_ids;               // chat -> admins
    std::map<std::pair<i64, i64>, MemberStatus> status;   // (chat,user) override
    std::set<i64> assistant_in;                           // chats where assistants already are
    std::map<i64, User> users;

    std::vector<Event> events;                            // everything the bot did
    std::mutex mu;

    User me() override { return bot_user; }
    int assistants() override { return static_cast<int>(assistant_users.size()); }
    User assistant(int idx) override { return assistant_users.at(static_cast<size_t>(idx) - 1); }

    Message send_message(i64 chat, const std::string& html, const Keyboard& kb, i64) override;
    Message send_photo(i64 chat, const std::string& photo, const std::string& caption, const Keyboard& kb, i64) override;
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
    std::string invite_link(i64 chat) override { return "https://t.me/+mock" + std::to_string(chat); }
    void approve_join_request(i64, i64) override {}
    void assistant_join(int idx, const std::string& link) override;
    void assistant_leave(int, i64) override {}
    bool assistant_send(int, i64, const std::string&) override { return true; }
    std::vector<i64> assistant_groups(int) override { return {}; }
    void leave_chat(i64 chat) override;
    std::optional<User> get_user(const std::string& username) override;
    std::optional<User> get_user(i64 id) override;
    std::optional<Chat> get_chat(const std::string&) override { return std::nullopt; }
    void copy_message(i64 to, const Message& m) override;
    void forward_message(i64 to, const Message& m) override;
    bool download_media(const Message& m, const std::string& path, const std::function<void(Progress)>& progress,
                        const std::atomic<bool>& cancel) override;
    void run() override;
    void stop() override;

    // ---- test helpers ----
    void inject(const Message& m) { if (on_message) on_message(m); }
    void press(const CallbackQuery& q) { if (on_callback) on_callback(q); }
    std::vector<Event> snapshot() { std::lock_guard<std::mutex> lk(mu); return events; }
    std::string dump_last(size_t n = 5);
    i64 last_mid(i64 chat);
    std::string joined_chat_for_assistants;
    std::vector<i64> left_chats, deleted;
    std::vector<std::string> answers;

private:
    Message make(i64 chat, const std::string& text);
    void log(Event e) { std::lock_guard<std::mutex> lk(mu); events.push_back(std::move(e)); }
    std::atomic<i64> next_id_{100};
    std::mutex run_mu_;
    std::condition_variable run_cv_;
    bool stopped_ = false;
};

}  // namespace bot::tg
