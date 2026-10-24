#include "botapi_client.hpp"
#include <chrono>
#include <filesystem>
#include <thread>
#include "../core/log.hpp"
#include "../core/util.hpp"
#include "../net/http.hpp"

namespace bot::tg {
using nlohmann::json;

namespace {

bool icontains(const std::string& hay, const char* needle) { return util::lower(hay).find(needle) != std::string::npos; }

Err classify(long code, const std::string& d) {
    if (code == 429) return Err::FloodWait;
    if (icontains(d, "message is not modified")) return Err::MessageNotModified;
    if (icontains(d, "message to edit not found") || icontains(d, "message_id_invalid") ||
        icontains(d, "message can't be edited") || icontains(d, "message to delete not found") ||
        icontains(d, "message identifier is not specified"))
        return Err::MessageIdInvalid;
    if (icontains(d, "not enough rights to send photo") || icontains(d, "not enough rights to send media") ||
        icontains(d, "not enough rights to send documents"))
        return Err::ChatSendMediaForbidden;
    if (icontains(d, "administrator rights") || icontains(d, "chat_admin_required") ||
        icontains(d, "not enough rights") || icontains(d, "need to be an administrator"))
        return Err::ChatAdminRequired;
    if (icontains(d, "have no rights to send") || icontains(d, "chat_write_forbidden")) return Err::ChatWriteForbidden;
    if (icontains(d, "was blocked") || icontains(d, "was kicked") || icontains(d, "bot is not a member") || code == 403)
        return Err::Forbidden;
    if (icontains(d, "chat not found") || icontains(d, "channel_invalid")) return Err::ChannelInvalid;
    if (icontains(d, "user_not_participant") || icontains(d, "participant_id_invalid")) return Err::UserNotParticipant;
    if (icontains(d, "query is too old") || icontains(d, "query id is invalid")) return Err::QueryIdInvalid;
    return Err::Other;
}

bool media_url_problem(const std::string& d) {
    return icontains(d, "failed to get http url content") || icontains(d, "wrong file identifier") ||
           icontains(d, "wrong type of the web page content") || icontains(d, "wrong remote file identifier") ||
           icontains(d, "image_process_failed") || icontains(d, "webpage_curl_failed") ||
           icontains(d, "webpage_media_empty") || icontains(d, "photo_invalid_dimensions");
}

User parse_user(const json& j) {
    User u;
    u.id = j.value("id", 0LL);
    u.is_bot = j.value("is_bot", false);
    u.first_name = j.value("first_name", "");
    u.username = j.value("username", "");
    return u;
}

Chat parse_chat(const json& j) {
    Chat c;
    c.id = j.value("id", 0LL);
    std::string t = j.value("type", "private");
    c.type = t == "private" ? ChatType::Private : t == "group" ? ChatType::Group : t == "supergroup" ? ChatType::Supergroup : ChatType::Channel;
    c.title = j.value("title", j.value("first_name", ""));
    c.username = j.value("username", "");
    return c;
}

std::vector<Entity> parse_entities(const json& arr) {
    std::vector<Entity> out;
    if (!arr.is_array()) return out;
    for (const auto& e : arr) {
        Entity en;
        std::string t = e.value("type", "");
        en.offset = e.value("offset", 0);
        en.length = e.value("length", 0);
        if (t == "url") en.type = Entity::Type::Url;
        else if (t == "text_link") { en.type = Entity::Type::TextLink; en.url = e.value("url", ""); }
        else if (t == "text_mention" && e.contains("user")) { en.type = Entity::Type::TextMention; en.user = parse_user(e["user"]); }
        out.push_back(std::move(en));
    }
    return out;
}

}  // namespace

BotApiClient::BotApiClient(std::string token, std::string base_url, voice::AssistantManager* a)
    : token_(std::move(token)), base_(std::move(base_url)), a_(a) {
    while (!base_.empty() && base_.back() == '/') base_.pop_back();
    local_server_ = base_.find("api.telegram.org") == std::string::npos;
}

json BotApiClient::call(const std::string& method, const json& params) {
    auto r = net::post_json(base_ + "/bot" + token_ + "/" + method, params, 40, &stop_);
    if (r.status == 0) throw Error(Err::Other, "network: " + r.error);
    json j;
    try { j = json::parse(r.body); }
    catch (...) { throw Error(Err::Other, "bad Bot API reply (HTTP " + std::to_string(r.status) + ")"); }
    if (j.value("ok", false)) return j.contains("result") ? j["result"] : json();
    std::string desc = j.value("description", "unknown error");
    long code = j.value("error_code", r.status);
    int wait = j.contains("parameters") ? j["parameters"].value("retry_after", 0) : 0;
    throw Error(classify(code, desc), desc, wait);
}

bool BotApiClient::init(std::string* err) {
    try {
        me_ = parse_user(call("getMe", json::object()));
        return me_.id != 0;
    } catch (const std::exception& e) {
        if (err) *err = e.what();
        return false;
    }
}

// ---------------------------------------------------------------- parsing
json BotApiClient::keyboard_json(const Keyboard& kb) {
    json rows = json::array();
    for (const auto& row : kb) {
        json r = json::array();
        for (const auto& b : row) {
            json o{{"text", b.text}};
            if (!b.url.empty()) o["url"] = b.url;
            else if (!b.copy_text.empty()) o["copy_text"] = {{"text", b.copy_text}};
            else o["callback_data"] = b.callback_data.substr(0, 64);   // Bot API limit
            r.push_back(std::move(o));
        }
        rows.push_back(std::move(r));
    }
    return {{"inline_keyboard", rows}};
}

Message BotApiClient::parse_message(const json& j) {
    Message m;
    m.id = j.value("message_id", 0LL);
    if (j.contains("chat")) m.chat = parse_chat(j["chat"]);
    if (j.contains("from")) m.from = parse_user(j["from"]);
    m.text = j.value("text", j.value("caption", ""));
    m.entities = parse_entities(j.contains("entities") ? j["entities"] : j.value("caption_entities", json::array()));
    if (j.contains("reply_to_message") && j["reply_to_message"].is_object())
        m.reply_to = std::make_shared<Message>(parse_message(j["reply_to_message"]));

    for (const char* k : {"audio", "voice", "video", "document"}) {
        if (!j.contains(k)) continue;
        const json& f = j[k];
        m.has_media = true;
        m.media_file_id = f.value("file_id", "");
        m.media_unique_id = f.value("file_unique_id", "");
        m.media_mime = f.value("mime_type", "");
        m.media_size = f.value("file_size", 0LL);
        m.media_duration = f.value("duration", 0);
        std::string name = f.value("file_name", "");
        m.media_title = f.value("title", name);
        auto dot = name.find_last_of('.');
        m.media_ext = dot == std::string::npos ? "" : name.substr(dot + 1);
        if (m.media_ext.empty()) {
            if (std::string(k) == "voice") m.media_ext = "ogg";
            else if (m.media_mime == "audio/mpeg") m.media_ext = "mp3";
            else if (m.media_mime == "video/mp4") m.media_ext = "mp4";
        }
        break;
    }
    if (j.contains("new_chat_members"))
        for (const auto& u : j["new_chat_members"]) m.new_chat_members.push_back(parse_user(u));
    m.video_chat_started = j.contains("video_chat_started");
    m.video_chat_ended = j.contains("video_chat_ended");

    std::string cid = std::to_string(m.chat.id);
    if (!m.chat.username.empty()) m.link = "https://t.me/" + m.chat.username + "/" + std::to_string(m.id);
    else if (util::starts_with(cid, "-100")) m.link = "https://t.me/c/" + cid.substr(4) + "/" + std::to_string(m.id);
    return m;
}

void BotApiClient::remember(i64 chat, i64 mid, const std::string& html) {
    std::lock_guard<std::mutex> lk(cache_mu_);
    auto key = std::make_pair(chat, mid);
    if (!html_cache_.count(key)) {
        html_order_.push_back(key);
        if (html_order_.size() > 2000) { html_cache_.erase(html_order_.front()); html_order_.pop_front(); }
    }
    html_cache_[key] = html;
}

std::string BotApiClient::html_of(i64 chat, i64 mid) {
    std::lock_guard<std::mutex> lk(cache_mu_);
    auto it = html_cache_.find({chat, mid});
    return it == html_cache_.end() ? "" : it->second;
}

Message BotApiClient::sent_message(const json& result, const std::string& html) {
    Message m = parse_message(result);
    m.text = html;
    remember(m.chat.id, m.id, html);
    return m;
}

// ---------------------------------------------------------------- sending
Message BotApiClient::send_message(i64 chat, const std::string& html, const Keyboard& kb, i64 reply_to) {
    json p{{"chat_id", chat}, {"text", html}, {"parse_mode", "HTML"}, {"link_preview_options", {{"is_disabled", true}}}};
    if (!kb.empty()) p["reply_markup"] = keyboard_json(kb);
    if (reply_to) p["reply_parameters"] = {{"message_id", reply_to}, {"allow_sending_without_reply", true}};
    return sent_message(call("sendMessage", p), html);
}

Message BotApiClient::send_photo(i64 chat, const std::string& photo, const std::string& caption, const Keyboard& kb, i64 reply_to) {
    json p{{"chat_id", chat}, {"photo", photo}, {"caption", caption}, {"parse_mode", "HTML"}};
    if (!kb.empty()) p["reply_markup"] = keyboard_json(kb);
    if (reply_to) p["reply_parameters"] = {{"message_id", reply_to}, {"allow_sending_without_reply", true}};
    try {
        return sent_message(call("sendPhoto", p), caption);
    } catch (const Error& e) {
        // dead/unsupported image URL: still deliver the text
        if (e.kind == Err::Other && media_url_problem(e.what())) return send_message(chat, caption, kb, reply_to);
        throw;
    }
}

void BotApiClient::send_document(i64 chat, const std::string& path, const std::string& caption) {
    auto r = net::post_multipart(base_ + "/bot" + token_ + "/sendDocument",
                                 {{"chat_id", std::to_string(chat)}, {"caption", caption}, {"parse_mode", "HTML"}},
                                 {{"document", path, std::filesystem::path(path).filename().string()}});
    if (r.status == 0) throw Error(Err::Other, "network: " + r.error);
    json j = json::parse(r.body, nullptr, false);
    if (j.is_discarded() || !j.value("ok", false))
        throw Error(Err::Other, j.is_discarded() ? "bad reply" : j.value("description", "sendDocument failed"));
}

json BotApiClient::edit_common(i64 chat, i64 mid, const std::string& method, const std::string& field,
                               const std::string& html, const Keyboard& kb) {
    json p{{"chat_id", chat}, {"message_id", mid}, {field, html}, {"parse_mode", "HTML"}};
    if (field == "text") p["link_preview_options"] = {{"is_disabled", true}};
    p["reply_markup"] = keyboard_json(kb);   // always sent: omitting it would strip the buttons
    return call(method, p);
}

void BotApiClient::edit_text(i64 chat, i64 mid, const std::string& html, const Keyboard& kb) {
    try {
        edit_common(chat, mid, "editMessageText", "text", html, kb);
    } catch (const Error& e) {
        // the target is a photo card: its "text" lives in the caption
        if (e.kind == Err::Other && icontains(e.what(), "there is no text in the message")) {
            edit_common(chat, mid, "editMessageCaption", "caption", html, kb);
        } else throw;
    }
    remember(chat, mid, html);
}

void BotApiClient::edit_caption(i64 chat, i64 mid, const std::string& html, const Keyboard& kb) {
    try {
        edit_common(chat, mid, "editMessageCaption", "caption", html, kb);
    } catch (const Error& e) {
        if (e.kind == Err::Other && (icontains(e.what(), "there is no caption") || icontains(e.what(), "no caption in the message"))) {
            edit_common(chat, mid, "editMessageText", "text", html, kb);
        } else throw;
    }
    remember(chat, mid, html);
}

void BotApiClient::edit_photo(i64 chat, i64 mid, const std::string& photo, const std::string& caption, const Keyboard& kb) {
    json p{{"chat_id", chat}, {"message_id", mid},
           {"media", {{"type", "photo"}, {"media", photo}, {"caption", caption}, {"parse_mode", "HTML"}}},
           {"reply_markup", keyboard_json(kb)}};
    try {
        call("editMessageMedia", p);
        remember(chat, mid, caption);
    } catch (const Error& e) {
        if (e.kind == Err::Other && media_url_problem(e.what())) { edit_text(chat, mid, caption, kb); return; }   // text-only card
        if (e.kind == Err::Other && icontains(e.what(), "message can't be edited")) throw Error(Err::MessageIdInvalid, e.what());
        throw;
    }
}

void BotApiClient::edit_markup(i64 chat, i64 mid, const Keyboard& kb) {
    call("editMessageReplyMarkup", {{"chat_id", chat}, {"message_id", mid}, {"reply_markup", keyboard_json(kb)}});
}

void BotApiClient::delete_messages(i64 chat, const std::vector<i64>& ids) {
    std::optional<Error> first;
    for (i64 id : ids) {
        try { call("deleteMessage", {{"chat_id", chat}, {"message_id", id}}); }
        catch (const Error& e) { if (!first) first = e; }
    }
    if (first) throw *first;
}

void BotApiClient::answer_callback(const std::string& id, const std::string& text, bool alert) {
    json p{{"callback_query_id", id}};
    if (!text.empty()) { p["text"] = text.substr(0, 200); p["show_alert"] = alert; }
    call("answerCallbackQuery", p);
}

void BotApiClient::answer_callback_url(const std::string& id, const std::string& url) {
    call("answerCallbackQuery", {{"callback_query_id", id}, {"url", url}});
}

// ---------------------------------------------------------------- chat info
MemberStatus BotApiClient::member_status(i64 chat, i64 user) {
    json r = call("getChatMember", {{"chat_id", chat}, {"user_id", user}});
    std::string s = r.value("status", "left");
    if (s == "creator") return MemberStatus::Owner;
    if (s == "administrator") return MemberStatus::Admin;
    if (s == "member") return MemberStatus::Member;
    if (s == "restricted") return r.value("is_member", true) ? MemberStatus::Restricted : MemberStatus::NotMember;
    if (s == "kicked") return MemberStatus::Banned;
    return MemberStatus::NotMember;   // left
}

std::vector<i64> BotApiClient::admins(i64 chat) {
    std::vector<i64> out;
    for (const auto& m : call("getChatAdministrators", {{"chat_id", chat}})) {
        if (!m.contains("user")) continue;
        User u = parse_user(m["user"]);
        if (!u.is_bot) out.push_back(u.id);
    }
    return out;
}

void BotApiClient::unban(i64 chat, i64 user) { call("unbanChatMember", {{"chat_id", chat}, {"user_id", user}, {"only_if_banned", true}}); }

std::string BotApiClient::invite_link(i64 chat) {
    json c = call("getChat", {{"chat_id", chat}});
    std::string link = c.value("invite_link", "");
    if (!link.empty()) return link;
    return call("exportChatInviteLink", {{"chat_id", chat}}).get<std::string>();
}

void BotApiClient::approve_join_request(i64 chat, i64 user) { call("approveChatJoinRequest", {{"chat_id", chat}, {"user_id", user}}); }
void BotApiClient::leave_chat(i64 chat) { call("leaveChat", {{"chat_id", chat}}); }

void BotApiClient::assistant_join(int idx, const std::string& link) {
    if (!a_) throw Error(Err::Other, "no assistant configured");
    json body;
    if (util::starts_with(link, "http") || link.find("t.me/") != std::string::npos || (!link.empty() && link[0] == '+'))
        body["invite_link"] = link;
    else body["username"] = link;
    json r;
    try { r = a_->call(idx, "/join", body, 60); }
    catch (const std::exception& e) { throw Error(Err::Other, e.what()); }
    if (r.value("ok", false)) return;
    std::string err = r.value("error", "join failed");
    if (icontains(err, "already") || icontains(err, "user_already_participant")) throw Error(Err::UserAlreadyParticipant, err);
    if (icontains(err, "invite_request_sent") || icontains(err, "request to join")) throw Error(Err::InviteRequestSent, err);
    throw Error(Err::Other, err);
}

void BotApiClient::assistant_leave(int idx, i64 chat) {
    if (!a_) return;
    try {
        json r = a_->call(idx, "/leave_chat", {{"chat_id", chat}}, 60);
        if (!r.value("ok", false)) log::warn("assistant_leave(" + std::to_string(idx) + ", " + std::to_string(chat) + "): " + r.value("error", "failed"));
    } catch (const std::exception& e) { log::warn(std::string("assistant_leave: ") + e.what()); }
}

bool BotApiClient::assistant_send(int idx, i64 chat, const std::string& text) {
    if (!a_) return false;
    try {
        json r = a_->call(idx, "/send_message", {{"chat_id", chat}, {"text", text}}, 45);
        if (r.value("ok", false)) return true;
        log::warn("assistant_send(" + std::to_string(idx) + "): " + r.value("error", "failed"));
    } catch (const std::exception& e) { log::warn(std::string("assistant_send: ") + e.what()); }
    return false;
}

std::vector<i64> BotApiClient::assistant_groups(int idx) {
    std::vector<i64> out;
    if (!a_) return out;
    try {
        json r = a_->call(idx, "/dialogs", json::object(), 120);
        if (r.value("ok", false)) for (const auto& c : r["chats"]) out.push_back(c.get<i64>());
    } catch (const std::exception& e) { log::warn(std::string("assistant_groups: ") + e.what()); }
    return out;
}

std::optional<User> BotApiClient::get_user(const std::string& username) {
    try {
        json c = call("getChat", {{"chat_id", username}});
        if (c.value("type", "") != "private") return std::nullopt;
        return parse_user(c);
    } catch (...) { return std::nullopt; }
}
std::optional<User> BotApiClient::get_user(i64 id) {
    try {
        json c = call("getChat", {{"chat_id", id}});
        return parse_user(c);
    } catch (...) { return std::nullopt; }
}
std::optional<Chat> BotApiClient::get_chat(const std::string& username) {
    try { return parse_chat(call("getChat", {{"chat_id", username}})); } catch (...) { return std::nullopt; }
}

void BotApiClient::copy_message(i64 to, const Message& m) {
    json p{{"chat_id", to}, {"from_chat_id", m.chat.id}, {"message_id", m.id}};
    if (!m.reply_markup.empty()) p["reply_markup"] = keyboard_json(m.reply_markup);
    call("copyMessage", p);
}
void BotApiClient::forward_message(i64 to, const Message& m) {
    call("forwardMessage", {{"chat_id", to}, {"from_chat_id", m.chat.id}, {"message_id", m.id}});
}

bool BotApiClient::download_media(const Message& m, const std::string& path, const std::function<void(Progress)>& progress,
                                  const std::atomic<bool>& cancel) {
    if (m.media_file_id.empty()) return false;
    json f;
    try { f = call("getFile", {{"file_id", m.media_file_id}}); }
    catch (const Error& e) { log::warn(std::string("getFile failed: ") + e.what()); return false; }
    std::string fp = f.value("file_path", "");
    if (fp.empty()) return false;
    if (!fp.empty() && fp[0] == '/') {   // local Bot API server: file already on disk
        std::error_code ec;
        std::filesystem::copy_file(fp, path, std::filesystem::copy_options::overwrite_existing, ec);
        return !ec;
    }
    return net::download(base_ + "/file/bot" + token_ + "/" + fp, path,
                         [&](i64 cur, i64 total) { if (progress) progress({cur, total ? total : m.media_size}); }, cancel);
}

// ---------------------------------------------------------------- updates
void BotApiClient::handle_update(const json& u) {
    try {
        if (u.contains("message") && on_message) {
            on_message(parse_message(u["message"]));
        } else if (u.contains("my_chat_member") && on_bot_member) {
            const json& c = u["my_chat_member"];
            BotMemberChange ch;
            ch.chat = parse_chat(c["chat"]);
            if (c.contains("from")) ch.by = parse_user(c["from"]);
            ch.old_status = c.value("old_chat_member", json::object()).value("status", "");
            ch.new_status = c.value("new_chat_member", json::object()).value("status", "");
            on_bot_member(ch);
        } else if (u.contains("callback_query") && on_callback) {
            const json& c = u["callback_query"];
            CallbackQuery q;
            q.id = c.value("id", "");
            q.from = parse_user(c["from"]);
            q.data = c.value("data", "");
            if (!c.contains("message")) return;   // inline-mode callbacks are not supported
            q.message = parse_message(c["message"]);
            std::string html = html_of(q.message.chat.id, q.message.id);
            if (!html.empty()) q.message.text = html;   // Bot API gives plain text; handlers expect the HTML we sent
            on_callback(q);
        }
    } catch (const std::exception& e) {
        log::error(std::string("update handling failed: ") + e.what());
    }
}

void BotApiClient::run() {
    i64 offset = 0;
    int backoff = 1;
    log::info("Polling Telegram updates...");
    while (!stop_) {
        try {
            json r = call("getUpdates", {{"offset", offset}, {"timeout", 25},
                                         {"allowed_updates", json::array({"message", "callback_query", "my_chat_member"})}});
            backoff = 1;
            for (const auto& u : r) {
                offset = std::max<i64>(offset, u.value("update_id", 0LL) + 1);
                handle_update(u);
            }
        } catch (const Error& e) {
            if (stop_) break;
            int wait = e.kind == Err::FloodWait ? std::max(e.wait, 1) : backoff;
            log::warn(std::string("getUpdates: ") + e.what() + " (retry in " + std::to_string(wait) + "s)");
            for (int i = 0; i < wait * 10 && !stop_; ++i) std::this_thread::sleep_for(std::chrono::milliseconds(100));
            backoff = std::min(backoff * 2, 30);
        }
    }
}

void BotApiClient::stop() { stop_ = true; }

}  // namespace bot::tg
