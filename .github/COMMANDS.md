<h1 align="center">🎧 NØX BEATS – Commands</h1>

<p align="center">
  Every command starts with <code>/</code>. This page explains who can use each command, how to write it, and what it does.
</p>

<p align="center">
  <a href="README.md"><img src="https://img.shields.io/badge/Back%20to-README-181717?style=for-the-badge&logo=github&logoColor=white" alt="Back to README"></a>
  <a href="MORE.md"><img src="https://img.shields.io/badge/More%20Details-MORE.md-181717?style=for-the-badge" alt="More details"></a>
</p>

## Contents

[Basics](#basics) · [Play music](#-play-music) · [Control playback](#-control-playback) · [Autoplay and thumbnail](#-autoplay-and-thumbnail) · [Authorized users](#-authorized-users) · [Settings and menus](#-settings-and-menus) · [Information](#-information) · [Sudo commands](#-sudo-commands) · [Owner commands](#-owner-commands)

## Basics

**Access levels**

| Level | Who can use it |
| :-- | :-- |
| Everyone | Any member of the chat |
| Admin | Group admins, users added with `/auth`, and sudo users |
| Sudo | Users added with `/addsudo`, and the owner |
| Owner | The user set in `OWNER_ID` |

**Where commands work**

| Place | Commands |
| :-- | :-- |
| Supergroups only | `/play`, `/vplay`, `/playforce`, `/vplayforce`, playback control, `/queue`, `/auth` family, `/reload`, `/playmode` |
| Private chat only | `/help` |
| Anywhere | `/start`, `/lang`, `/ping`, `/stats`, `/sudolist`, sudo and owner commands |

**Choosing a user.** Commands that need a user (`/auth`, `/unauth`, `/addsudo`, `/rmsudo`) accept any one of these:

- reply to a message from that user
- `@username`
- the numeric user id

**Blocked users.** Users blocked with `/blacklist` are ignored by the bot (they only get a notice from `/start`).

**Group requirement.** The bot only works in supergroups. If it is added to or used in a normal group, it replies that the chat is not supported and leaves.

---

## 🎵 Play music

| Command | Access | What it does |
| :-- | :-: | :-- |
| `/play <song or link>` | Everyone* | Searches YouTube (or uses the link) and plays or queues the audio |
| `/vplay <song or link>` | Everyone* | Same as `/play`, with video (needs `VIDEO_PLAY` on) |
| `/playforce <song or link>` | Admin | Plays immediately instead of queueing |
| `/vplayforce <song or link>` | Admin | Video version of `/playforce` |

**Ways to use it**

```text
/play attention
/play https://www.youtube.com/watch?v=...
/play -f attention          force play (same as /playforce)
/play -v attention          video (same as /vplay)
```

You can also reply to an **audio or video file** (or a message with a stream link) with `/play`.

**Good to know**

- \* If the chat's play mode is set to admins only (`/playmode`), `/play` needs Admin access too.
- A track longer than `DURATION_LIMIT` (default 60 minutes) is refused.
- The queue holds up to `QUEUE_LIMIT` tracks (default 20). A playlist adds up to `PLAYLIST_LIMIT` tracks (default 20).
- If the chat has command delete turned on, the bot removes your command message after reading it.
- The assistant account must be able to join the group. A voice chat must already be running.

## ⏯️ Control playback

All of these need **Admin** access, except `/queue`.

| Command | What it does |
| :-- | :-- |
| `/pause` | Pauses the stream |
| `/resume` | Resumes the stream |
| `/skip`, `/next` | Plays the next track in the queue |
| `/stop`, `/end` | Stops the stream and clears the queue |
| `/loop <1-10>` | Repeats the current track that many times |
| `/loop off` | Turns repeat off |
| `/loop` | Shows the current repeat count |
| `/seek <seconds>` | Jumps forward in the track |
| `/seekback <seconds>` | Jumps back in the track |
| `/queue`, `/playing` | Shows the current track and the queue (Everyone) |

**Examples**

```text
/loop 3          repeat the current track 3 times
/loop off        stop repeating
/seek 30         jump 30 seconds forward
/seekback 15     jump 15 seconds back
```

**Notes**

- `/seek` and `/seekback` need at least **10 seconds**, and only work on tracks with a known length.
- The loop count is limited to 10.
- The player message also has inline buttons for pause, resume, skip, stop and replay. They follow the same access rules.

## 🧰 Queue tools and extras

Need **Admin** (or whatever `/adminmode` allows) unless noted.

| Command | What it does |
| :-- | :-- |
| `/clear` / `/restore` | Empty the waiting queue / bring the last cleared queue back |
| `/remove [n]` | Remove the track at position `n` of `/queue` |
| `/move [from] [to]` | Reorder two waiting tracks |
| `/shuffle` | Shuffle the waiting tracks |
| `/pos` | Current position, length and speed |
| `/jump [time]` | Jump to an exact time (`90` or `1:30`) |
| `/speed [0.5-2.0]` | Change the playback speed (aliases `/setspeed`, `/speedup`) |
| `/bass [0-100\|on\|off]` | Bass boost for the running session (1 is already audible, 100 is very heavy) (also `/cbass` for channels); resets when the session ends |
| `/mute` / `/unmute` | Mute or unmute the stream without leaving the chat |
| `/replay` | Play the current track again |
| `/player` | Send the player card again |
| `/fplay`, `/fvplay` | Short names for force play / force video play |

## 🎼 Personal playlists

Every user has their own playlist, saved in the database (up to 100 songs). It works in any chat and in private.

| Command | What it does |
| :-- | :-- |
| `/addplaylist [song name or link]` | Save a song. With no argument it saves the song that is playing now. Spotify, Apple Music, JioSaavn, SoundCloud and YouTube playlist links save all their songs (up to `PLAYLIST_LIMIT`) |
| `/playlist` | Show your playlist (also `/myplaylist`, `/playlists`) |
| `/rmplaylist [number \| name \| all]` | Remove a song, or everything (also `/clearplaylist`) |
| `/playplaylist [-s] [-v] [-f]` | Queue your playlist in the group. `-s` shuffles, `-v` plays video, `-f` forces it first |

## 🌐 Supported sources

YouTube (search, links, playlists), Spotify, Apple Music, JioSaavn, SoundCloud, direct `.m3u8` / stream links, and Telegram audio or video files. Spotify, Apple Music, JioSaavn and SoundCloud links are read for their title and artist, and the audio is played from the best YouTube match. Albums and playlists add up to `PLAYLIST_LIMIT` songs.

## 🔗 Spotify

`/play` and `/vplay` accept `open.spotify.com` track, album and playlist links. The bot reads the title and artists from Spotify and plays the best match from YouTube. Albums and playlists add up to `PLAYLIST_LIMIT` tracks. Setting `SPOTIFY_CLIENT_ID` and `SPOTIFY_CLIENT_SECRET` (free, from developer.spotify.com) makes this more reliable; without them the bot falls back to Spotify's public embed page.

## 📺 Channel play

1. In the group, an admin of both the group and the channel sends `/channelplay [channel id or @username]`. The bot and the assistant must be in the channel.
2. Use the `c` commands: `/cplay`, `/cvplay`, `/cfplay`, `/cpause`, `/cresume`, `/cskip`, `/cstop`, `/cqueue`, `/cseek`, `/cspeed`, `/cmute` ... They are the normal commands, but they play in the channel's video chat.
3. `/channelplay off` unlinks it. `/userbot` (`/cjoin` for the channel) asks the assistant to join.

A chat plays in either the group's video chat or the channel's at one time.

## 🗂️ Chat settings

| Command | What it does |
| :-- | :-- |
| `/adminmode [admins\|auth\|everyone]` | Who may control playback |
| `/playmode [admin\|everyone]` | Who may start playback |
| `/cmddelete [on\|off]` | Delete command messages |
| `/cleanmode [on\|off\|minutes]` | Delete the bot's own messages after a while (the player card stays) |
| `/nothumb [on\|off]` | Hide the player thumbnail |

## 📡 RTMP streaming

1. In the bot's **private chat**: `/setrtmp [chat id] [rtmp://host/app] [stream key]`. You must be an admin of that chat. `/setrtmp [chat id] off` removes it.
2. In the group: `/stream [song or url]` pushes the video to that RTMP target (for example a Telegram live stream or any RTMP server). `/streamstop` stops it, `/streamstatus` shows it.

The stream key is kept in the bot's database and is never printed back. Needs `ffmpeg` and enough CPU for libx264.

## 🧪 Bot tools

The bot sends logs to `LOGGER_ID` (turn them off with `/logger off`): new chats and users, every play request (song, group id and name, who asked), and when the bot is removed from a group or blocked by a user.

Sudo: `/maintenance [on|off] [reason]`, `/autoleave [on|off]`, `/json`, `/speedtest`, `/blacklisted`. Everyone: `/bug [text]` (sent to the logger chat) and `/privacy`.

## ♬ Autoplay and thumbnail

| Command | Access | What it does |
| :-- | :-: | :-- |
| `/autoplay` | Everyone | Shows whether autoplay is on, plus its language and mood |
| `/autoplay on` / `off` | Admin or authorized | When the queue ends, related songs keep playing |
| `/autoplay lang <language>` | Admin or authorized | Prefer one language, for example `hindi` or `punjabi`. `auto` removes the preference |
| `/autoplay mood <mood>` | Admin or authorized | Prefer one mood, for example `sad` or `party`. `any` removes the preference |
| `/thumbnail` | Everyone | Shows whether the thumbnail is on |
| `/thumbnail on` / `off` | Admin or authorized | Show the thumbnail on the player card, or text only. Applies to the next songs |

The player card has two switches for the same settings: **Autoplay | On/Off** and **Thumb | On/Off**. Autoplay never repeats a recent song and skips mixes, jukeboxes, shorts and live streams. Both settings are saved per chat.

## 👮 Authorized users

Authorized users can use the playback controls without being group admins.

| Command | Access | What it does |
| :-- | :-: | :-- |
| `/auth <user>` | Admin | Authorizes a user in this chat |
| `/unauth <user>` | Admin | Removes the authorization |
| `/authlist` | Admin | Lists all authorized users of this chat |
| `/reload`, `/admincache` | Everyone | Refreshes the cached admin list |

**Notes**

- Group admins already have access, so `/auth` on an admin only tells you that they are an admin.
- `/reload` can be used once every 10 minutes per user. Use it after you change the admins of a group.
- Authorization is saved per chat in MongoDB.

## ⚙️ Settings and menus

| Command | Access | What it does |
| :-- | :-: | :-- |
| `/start` | Everyone | Start card with **Help**, **Support**, **Channel** and **Source** buttons |
| `/help` | Everyone | Help menu in private chat, with **Home** and **Close** buttons |
| `/lang`, `/language` | Everyone | Opens the language list. Changing it needs Admin access |
| `/playmode`, `/settings` | Everyone | Opens the chat settings panel |

**Settings panel** (per chat)

- **Play mode:** let everyone use `/play`, or only admins and authorized users.
- **Command delete:** remove command messages after they are processed.
- **Language:** one of 13 languages.

**Help menu.** `/help` edits the same message when you press a button. **Home** goes back to the start card and **Close** removes the message.

## 📊 Information

| Command | Access | What it does |
| :-- | :-: | :-- |
| `/ping`, `/alive` | Everyone | Latency, uptime, CPU, RAM and disk usage |
| `/stats` | Everyone | Bot statistics: assistants, served chats and users, blacklisted chats and users, sudo users. Sudo users also see memory and disk details |
| `/sudolist`, `/listsudo` | Everyone | Shows the owner and all sudo users |

## 🛡️ Sudo commands

| Command | What it does |
| :-- | :-- |
| `/ac` | Shows how many voice chats are active right now |
| `/activevc` | Lists the active voice chats with the track playing in each (sent as a file if the list is long) |
| `/broadcast` | Sends a message to every served chat. Reply to the message you want to send |
| `/blacklist <id>` | Blocks a chat or a user |
| `/unblacklist <id>`, `/whitelist <id>` | Removes the block |
| `/logs` | Sends the log file (`log.txt`) |
| `/logger on` / `/logger off` | Turns logger group reports on or off |
| `/restart` | Clears the `cache` and `downloads` folders and restarts the bot |

**`/broadcast` flags**

| Flag | Meaning |
| :-- | :-- |
| *(none)* | Forward the replied message to all served chats |
| `-copy` | Send it as a copy, without the "forwarded from" label |
| `-user` | Also send it to users who started the bot |
| `-nochat` | Skip chats (use with `-user` to message users only) |

```text
/broadcast -copy              copy to all chats
/broadcast -copy -user        copy to all chats and users
/broadcast -nochat -user      users only
```

Only one broadcast runs at a time. If some chats fail, the bot sends an `errors.txt` file with the reasons.

**`/blacklist` and `/unblacklist`**

```text
/blacklist -1001234567890     block a chat by id
/blacklist 123456789          block a user by id
/blacklist @groupusername     block a chat by username
```

A chat id starts with `-100`. Anything else is treated as a user id.

## 👑 Owner commands

| Command | What it does |
| :-- | :-- |
| `/addsudo <user>` | Adds a sudo user |
| `/delsudo <user>`, `/rmsudo <user>` | Removes a sudo user |
| `/eval <command>` (also `/ev`, `/sh`, `/exec`) | Runs a shell command on the server and shows the output |

**`/eval` details**

- Write the command after `/eval`, or reply to a message that contains it.
- Time limit: 60 seconds.
- The reply shows the input, the last 3000 characters of the output, the exit code and the run time.

```text
/eval ls -la
/eval df -h
```

> ⚠️ `/eval` runs real shell commands on your server. Keep `OWNER_ID` set to your own account only.
