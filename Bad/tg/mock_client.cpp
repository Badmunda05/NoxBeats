#include "mock_client.hpp"
#include <fstream>
#include <iostream>
#include "../core/util.hpp"

namespace bot::tg {

Message MockClient::make(i64 chat, const std::string& text) {
    Message m;
    m.id = next_id_++;
    m.chat.id = chat;
    m.chat.type = chat < 0 ? ChatType::Supergroup : ChatType::Private;
    m.from = bot_user;
    m.text = text;
    return m;
}

Message MockClient::send_message(i64 chat, const std::string& html, const Keyboard& kb, i64) {
    Message m = make(chat, html);
    log({"send", chat, m.id, html, "", kb});
    return m;
}
Message MockClient::send_photo(i64 chat, const std::string& photo, const std::string& caption, const Keyboard& kb, i64) {
    Message m = make(chat, caption);
    log({"photo", chat, m.id, caption, photo, kb});
    return m;
}
void MockClient::send_document(i64 chat, const std::string& path, const std::string& caption) { log({"doc", chat, 0, caption, path, {}}); }
void MockClient::edit_text(i64 chat, i64 mid, const std::string& html, const Keyboard& kb) { log({"edit", chat, mid, html, "", kb}); }
void MockClient::edit_caption(i64 chat, i64 mid, const std::string& html, const Keyboard& kb) { log({"edit_caption", chat, mid, html, "", kb}); }
void MockClient::edit_photo(i64 chat, i64 mid, const std::string& photo, const std::string& caption, const Keyboard& kb) { log({"edit_photo", chat, mid, caption, photo, kb}); }
void MockClient::edit_markup(i64 chat, i64 mid, const Keyboard& kb) { log({"edit_markup", chat, mid, "", "", kb}); }
void MockClient::delete_messages(i64 chat, const std::vector<i64>& ids) {
    std::lock_guard<std::mutex> lk(mu);
    for (i64 i : ids) { deleted.push_back(i); events.push_back({"delete", chat, i, "", "", {}}); }
}
void MockClient::answer_callback(const std::string&, const std::string& text, bool) {
    std::lock_guard<std::mutex> lk(mu);
    answers.push_back(text);
}
void MockClient::answer_callback_url(const std::string&, const std::string& url) { std::lock_guard<std::mutex> lk(mu); answers.push_back("url:" + url); }

MemberStatus MockClient::member_status(i64 chat, i64 user) {
    auto it = status.find({chat, user});
    if (it != status.end()) return it->second;
    for (const auto& a : assistant_users)
        if (a.id == user) return assistant_in.count(chat) ? MemberStatus::Member : MemberStatus::NotMember;
    if (admin_ids[chat].count(user)) return MemberStatus::Admin;
    return MemberStatus::Member;
}
std::vector<i64> MockClient::admins(i64 chat) { auto& s = admin_ids[chat]; return {s.begin(), s.end()}; }
void MockClient::unban(i64, i64) {}
void MockClient::assistant_join(int, const std::string& link) { joined_chat_for_assistants = link; }
void MockClient::leave_chat(i64 chat) { std::lock_guard<std::mutex> lk(mu); left_chats.push_back(chat); }
std::optional<User> MockClient::get_user(const std::string& username) {
    for (auto& [id, u] : users) if ("@" + u.username == username) return u;
    return std::nullopt;
}
std::optional<User> MockClient::get_user(i64 id) { auto it = users.find(id); return it == users.end() ? std::nullopt : std::optional<User>(it->second); }
void MockClient::copy_message(i64 to, const Message& m) { log({"copy", to, m.id, m.text, "", {}}); }
void MockClient::forward_message(i64 to, const Message& m) { log({"forward", to, m.id, m.text, "", {}}); }
bool MockClient::download_media(const Message& m, const std::string& path, const std::function<void(Progress)>& progress,
                                const std::atomic<bool>& cancel) {
    progress({m.media_size / 2, m.media_size});
    if (cancel) return false;
    std::ofstream(path) << "mock";
    return true;
}

void MockClient::run() {
    std::unique_lock<std::mutex> lk(run_mu_);
    run_cv_.wait(lk, [this] { return stopped_; });
}
void MockClient::stop() {
    { std::lock_guard<std::mutex> lk(run_mu_); stopped_ = true; }
    run_cv_.notify_all();
}

std::string MockClient::dump_last(size_t n) {
    auto ev = snapshot();
    std::string out;
    for (size_t i = ev.size() > n ? ev.size() - n : 0; i < ev.size(); ++i)
        out += "[" + ev[i].kind + " chat=" + std::to_string(ev[i].chat) + " mid=" + std::to_string(ev[i].mid) + "] " + ev[i].text + "\n";
    return out;
}
i64 MockClient::last_mid(i64 chat) {
    auto ev = snapshot();
    for (size_t i = ev.size(); i-- > 0;)
        if (ev[i].chat == chat && (ev[i].kind == "send" || ev[i].kind == "photo")) return ev[i].mid;
    return 0;
}

}  // namespace bot::tg
