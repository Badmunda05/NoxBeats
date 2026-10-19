<h1 align="center">🎧 NØX BEATS – More details</h1>

<p align="center">
  Extra information that is not in the README: features in detail, how NØX BEATS compares, building from source, every configuration variable and troubleshooting.
</p>

<p align="center">
  <a href="README.md"><img src="https://img.shields.io/badge/Back%20to-README-181717?style=for-the-badge&logo=github&logoColor=white" alt="Back to README"></a>
  <a href="COMMANDS.md"><img src="https://img.shields.io/badge/All%20Commands-COMMANDS.md-FF0000?style=for-the-badge" alt="All commands"></a>
</p>

## Contents

[Why C++](#why-c) · [Features in detail](#features-in-detail) · [Build from source](#build-from-source) · [Configuration reference](#configuration-reference) · [Cookies or API](#cookies-or-api) · [Data storage](#data-storage) · [More troubleshooting](#more-troubleshooting)

---

## Why C++

NØX BEATS is one native binary. The Bot API client, the in-process MTProto assistant and the ntgcalls voice engine run in a single process: no Python, no Go, no helper services.

| | **C++ (NØX BEATS)** | Go bots | Python bots (Pyrogram + PyTgCalls) |
| :-- | :-: | :-: | :-: |
| Runtime needed | none (native binary) | none (native binary) | Python + pip packages |
| Idle RAM | ~16 MB (measured)* | ~40–80 MB (typical) | ~150–300 MB (typical) |
| Start-up | instant | instant | slower (imports, interpreter) |
| CPU while idle | very low | low | higher (interpreter, GIL) |

<sub>* Measured idle in mock mode (no live Telegram, MongoDB or ffmpeg). A live deployment uses more, and streaming adds the memory of ffmpeg and ntgcalls in every language. Go and Python values are typical ranges, not measurements of specific bots.</sub>

## Features in detail

- **Playing:** `/play` and `/vplay` accept a song name, a YouTube link, a playlist, or a reply to an audio or video file.
- **Controls:** queue, pause, resume, skip, stop, seek, loop and replay, also available as inline buttons on the player message.
- **Access control:** group admins, authorized users per chat, sudo users, and a blacklist for chats and users.
- **Chat settings:** play mode (everyone or admins only), command delete and language, saved per chat.
- **Languages:** 13 interface languages, selectable with `/lang`.
- **Assistants:** up to 3 assistant accounts (string sessions). Each chat is bound to one of them, and every assistant joins the support group and updates channel when the bot starts.
- **Logger group:** `Bot Started`, `Assistant started`, new chats and new users are reported there.
- **Downloads:** your own download API first (`API_URL` + `API_KEY`), `yt-dlp` with cookies as the fallback. Search uses YouTube's InnerTube API and falls back to `yt-dlp`.
- **Persistence:** every setting is stored in MongoDB and survives restarts and redeploys.
- **Crash reporter:** a fatal signal prints `*** CRASH: SIG… ***` and a backtrace to the logs.
- **Console logs:** one line per event, `[18-Oct-26 15:00:35 - INFO] message`, without emoji.

## Build from source

Ubuntu 24.04:

```bash
sudo apt-get install -y g++ cmake make curl unzip ffmpeg \
    libcurl4-openssl-dev zlib1g-dev libmongoc-dev nlohmann-json3-dev pkg-config

# yt-dlp: standalone binary, no Python needed
sudo curl -fsSL https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp_linux -o /usr/local/bin/yt-dlp
sudo chmod +x /usr/local/bin/yt-dlp

# prebuilt libntgcalls (shared) + header
mkdir -p third_party/ntgcalls/lib third_party/ntgcalls/include /tmp/ntg
curl -fsSL https://github.com/pytgcalls/ntgcalls/releases/download/v2.1.0/ntgcalls.linux-x86_64-shared_libs.zip -o /tmp/ntg.zip
unzip -q /tmp/ntg.zip -d /tmp/ntg
cp "$(find /tmp/ntg -name libntgcalls.so | head -1)" third_party/ntgcalls/lib/
cp "$(find /tmp/ntg -name ntgcalls.h     | head -1)" third_party/ntgcalls/include/

cmake -B build -DCMAKE_BUILD_TYPE=Release -DWITH_NTGCALLS=ON
cmake --build build -j2 --target cpp-bot

export API_ID=… API_HASH=… BOT_TOKEN=… SESSION=… LOGGER_ID=… OWNER_ID=… MONGO_DB_URI=…
./build/cpp-bot
```

Keep `strings/` and `Bad/telegram_schema/` next to the binary.

**Docker with a pinned yt-dlp.** The image downloads the latest `yt-dlp` release. To pin a release:

```bash
docker build --build-arg YTDLP_VERSION=2026.08.19 -t noxbeats .
```

## Configuration reference

The required variables are listed in the README. Optional ones:

| Variable | Description |
| :-- | :-- |
| `SESSION2`, `SESSION3` | Extra assistant sessions |
| `SESSION_TYPE` | `pyrogram` (default) or `telethon` |
| `API_URL` | Download API base URL (for example `https://api.yourdomain.site`) |
| `API_KEY` | Download API key. Use your own API key |
| `COOKIES_URL` | batbin.me links to cookie files for `yt-dlp` |
| `YTDLP_UPDATE` | Run `yt-dlp -U` once at start (default `True`) |
| `DURATION_LIMIT` | Longest playable track in minutes (default `60`) |
| `QUEUE_LIMIT` | Max tracks per chat queue (default `20`) |
| `PLAYLIST_LIMIT` | Max tracks taken from a playlist (default `20`) |
| `AUTO_LEAVE` | Assistant leaves idle chats hourly (`True` / `False`) |
| `AUTO_END` | End the stream when the voice chat is empty |
| `THUMB_GEN` | Generate track thumbnails |
| `VIDEO_PLAY` | Allow `/vplay` |
| `LANG_CODE` | Default language (`en`, `hi`, `pa`, …) |
| `SUPPORT_CHANNEL` | Updates channel (default `https://t.me/PBX_UPDATE`) |
| `SUPPORT_CHAT` | Support group (default `https://t.me/PBXCHATS`) |
| `SOURCE_URL` | Link behind the **Source** button |

## Cookies or API

Use only `COOKIES_URL` (yt-dlp with cookies), or point `API_URL` + `API_KEY` at any download API. The bot calls:

```text
GET <API_URL>/download?url=<video_id>&type=audio|video&api_key=<API_KEY>
```

and expects the media file back. For an API with a different route or response, adapt `download_via_api()` in `Bad/platforms/yt.cpp`.

**yt-dlp.** The Docker image ships the standalone `yt-dlp` binary, so Python is not installed. YouTube changes often, so the bot checks for a newer `yt-dlp` at start (`YTDLP_UPDATE`). YouTube also asks datacenter IPs to prove they are not bots: set `COOKIES_URL` or `API_URL` + `API_KEY`, otherwise downloads can fail.

## Data storage

Sudo users, blacklist, authorized users, languages and the chat and user lists are saved in the `state` collection of your MongoDB database. `MongoDB connected successfully.` is logged only after a real connection and ping. If the database is unreachable the bot stops with an error instead of falling back to a file.

## More troubleshooting

The README lists the most common problems. These are the rest:

| Symptom | Check |
| :-- | :-- |
| `assistant 1: [406] AUTH_KEY_DUPLICATED` | The same `SESSION` runs somewhere else. Use a fresh string session for this bot |
| `AUTH_KEY_UNREGISTERED` / `SESSION_REVOKED` | The session was logged out; generate a new one |
| `Failed to process the query` | Search failed: look for `YouTube search request failed` or `yt-dlp search failed` in the logs |
| `API download failed` / `returned no media` | `API_URL` / `API_KEY` wrong; the bot falls back to `yt-dlp` (needs cookies on datacenter IPs) |
| `Assistant started` missing in the log group | The assistant account is not a member of `LOGGER_ID` |
| `MongoDB connection failed` on Atlas | Network Access → allow `0.0.0.0/0` (or your host's IP) |
| Crash | The logs show `*** CRASH: SIG… ***` with a backtrace; send those lines to the support group |
