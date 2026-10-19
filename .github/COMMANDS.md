<h1 align="center">🎧 NØX BEATS – Commands</h1>

<p align="center">
  Every command starts with <code>/</code>. This page explains who can use each command, how to write it, and what it does.
</p>

<p align="center">
  <a href="README.md"><img src="https://img.shields.io/badge/Back%20to-README-181717?style=for-the-badge&logo=github&logoColor=white" alt="Back to README"></a>
  <a href="MORE.md"><img src="https://img.shields.io/badge/More%20Details-MORE.md-181717?style=for-the-badge" alt="More details"></a>
</p>

## Contents

[Basics](#basics) · [Play music](#-play-music) · [Control playback](#-control-playback) · [Authorized users](#-authorized-users) · [Settings and menus](#-settings-and-menus) · [Information](#-information) · [Sudo commands](#-sudo-commands) · [Owner commands](#-owner-commands)

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
