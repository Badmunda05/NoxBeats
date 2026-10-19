// Shared data types: Telegram-side objects and queue items.
// Shared types (Media and Track are merged into one struct).
#pragma once
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace bot {

using i64 = std::int64_t;

enum class ChatType { Private, Group, Supergroup, Channel };

struct User {
    i64 id = 0;
    std::string first_name;
    std::string username;
    bool is_bot = false;

    std::string mention() const;  // HTML link, escaped
};

struct Chat {
    i64 id = 0;
    ChatType type = ChatType::Private;
    std::string title;
    std::string username;
};

struct Entity {
    enum class Type { Url, TextLink, TextMention, Other } type = Type::Other;
    int offset = 0;
    int length = 0;
    std::string url;       // TextLink
    User user;             // TextMention
};

struct Button {
    std::string text;
    std::string callback_data;  // either callback_data ...
    std::string url;            // ... or url
    std::string copy_text;      // ... or copy_text
};
using Keyboard = std::vector<std::vector<Button>>;

struct Message {
    i64 id = 0;
    Chat chat;
    std::optional<User> from;
    std::string text;      // text or caption
    std::vector<Entity> entities;
    std::vector<std::string> command;  // parsed: command[0] without "/" and "@bot"
    std::shared_ptr<Message> reply_to;

    // media (only what the music bot needs)
    bool has_media = false;
    std::string media_unique_id;
    std::string media_file_id;     // Bot API file_id (download handle)
    std::string media_ext;
    std::string media_title;
    std::string media_mime;
    i64 media_size = 0;
    int media_duration = 0;
    std::string link;      // t.me link of the message

    std::vector<User> new_chat_members;
    bool video_chat_started = false;
    bool video_chat_ended = false;
    Keyboard reply_markup;
};

struct CallbackQuery {
    std::string id;
    User from;
    Message message;   // the message the button is attached to
    std::string data;
};

// One queue item: a Telegram file / m3u8 stream or a YouTube track.
struct Track {
    std::string id;
    std::string channel_name;
    std::string duration = "00:00";
    int duration_sec = 0;
    std::string title;
    std::string url;
    std::string file_path;
    i64 message_id = 0;
    int time = 0;               // seconds played so far
    std::string thumbnail;
    std::string user;           // mention
    std::string view_count;
    bool video = false;
    bool is_youtube = true;     // false => Media (tg file / m3u8): default thumbnail
};
using TrackPtr = std::shared_ptr<Track>;

}  // namespace bot
