<h1 align="center">🎧 NØX BEATS</h1>

<p align="center">
  <img src="https://files.catbox.moe/z5febs.png" alt="NØX BEATS" width="600">
</p>

<p align="center">
  <b>Telegram voice-chat music bot written in C++20.</b><br>
  One binary: Bot API long polling + in-process MTProto assistant + ntgcalls voice. No Python, no Go, no helper processes.
</p>

<p align="center">
  <img src="https://img.shields.io/badge/language-C%2B%2B20-00599C?style=for-the-badge&logo=cplusplus" alt="C++20">
  <img src="https://img.shields.io/badge/voice-ntgcalls-8A2BE2?style=for-the-badge" alt="ntgcalls">
  <img src="https://img.shields.io/badge/database-MongoDB-47A248?style=for-the-badge&logo=mongodb&logoColor=white" alt="MongoDB">
  <img src="https://img.shields.io/badge/languages-13-orange?style=for-the-badge" alt="13 languages">
</p>

<p align="center">
  <a href="README.md"><img src="https://img.shields.io/badge/Back%20to-README-181717?style=for-the-badge&logo=github&logoColor=white" alt="Back to README"></a>
  <a href="COMMANDS.md"><img src="https://img.shields.io/badge/All%20Commands-COMMANDS.md-FF0000?style=for-the-badge" alt="All commands"></a>
  <a href="https://github.com/Badmunda05/NoxBeats/fork"><img src="https://img.shields.io/badge/Fork-This%20Repo-181717?style=for-the-badge&logo=github&logoColor=white" alt="Fork"></a>
  <a href="https://t.me/PBX_UPDATE"><img src="https://img.shields.io/badge/Updates-Channel-2CA5E0?style=for-the-badge&logo=telegram&logoColor=white" alt="Updates"></a>
  <a href="https://t.me/PBXCHATS"><img src="https://img.shields.io/badge/Support-Group-2CA5E0?style=for-the-badge&logo=telegram&logoColor=white" alt="Support"></a>
</p>

<img src="https://user-images.githubusercontent.com/73097560/115834477-dbab4500-a447-11eb-908a-139a6edaec5c.gif">

<p align="center"><img src="https://readme-typing-svg.herokuapp.com?color=FF0000&width=420&lines=%F0%9F%8E%A7+N%C3%98X+BEATS+%F0%9F%8E%A7;C%2B%2B20+Telegram+Music+Bot"></p>

<img src="https://user-images.githubusercontent.com/73097560/115834477-dbab4500-a447-11eb-908a-139a6edaec5c.gif">

<p align="center">•━━━━━━━━•••━━━━━━━━•</p>

<p align="center"><img src="https://readme-typing-svg.herokuapp.com?color=FF0000&width=420&lines=MADE+BY+BADMUNDA%E2%9D%A4%EF%B8%8F"></p>

<p align="center">•━━━━━━━━•••━━━━━━━━•</p>

<img src="https://user-images.githubusercontent.com/73097560/115834477-dbab4500-a447-11eb-908a-139a6edaec5c.gif">

<h2 align="center">─「 ⚡ ᴛʜᴇ ɴØx ʙᴇᴀᴛs ᴅɪғғᴇʀᴇɴᴄᴇ 」─</h2>

<p align="center">
  <b>NØX BEATS</b> is a lightweight open-source Telegram voice-chat music bot.<br>
  Written in C++20 on top of <a href="https://github.com/pytgcalls/ntgcalls">ntgcalls</a> (WebRTC), it prioritizes simple deployment and resource efficiency.
</p>

<table align="center">
  <tr>
    <th align="center" width="33%">Powerful</th>
    <th align="center" width="33%">Simple</th>
    <th align="center" width="33%">Light</th>
  </tr>
  <tr>
    <td align="center"><img src="https://raw.githubusercontent.com/pytgcalls/ntgcalls/master/.github/images/fast.gif" width="150" alt="Powerful"><br>Native C++20 binary: Bot API, MTProto assistant and ntgcalls voice in a single process</td>
    <td align="center"><img src="https://raw.githubusercontent.com/pytgcalls/ntgcalls/master/.github/images/simple.gif" width="150" alt="Simple"><br>Set the config vars and deploy. No Python needed</td>
    <td align="center"><img src="https://raw.githubusercontent.com/pytgcalls/ntgcalls/master/.github/images/light.gif" width="150" alt="Light"><br>Lighter and faster than Python and Go bots: about 16 MB RAM idle*</td>
  </tr>
</table>
<p align="center"><sub>Icons by the <a href="https://github.com/pytgcalls/ntgcalls">ntgcalls</a> project.</sub></p>

<div align="center">

| | **C++ (NØX BEATS)** | Go bots | Python bots (Pyrogram + PyTgCalls) |
| :-- | :-: | :-: | :-: |
| Runtime needed | none (native binary) | none (native binary) | Python + pip packages |
| Idle RAM | ~16 MB (measured)* | ~40–80 MB (typical) | ~150–300 MB (typical) |
| Start-up | instant | instant | slower (imports, interpreter) |
| CPU while idle | very low | low | higher (interpreter, GIL) |

<sub>* Measured idle in mock mode (no live Telegram, MongoDB or ffmpeg). A live deployment uses more, and streaming adds the memory of ffmpeg and ntgcalls in every language. Go and Python values are typical ranges, not measurements of specific bots.</sub>

</div>

<img src="https://user-images.githubusercontent.com/73097560/115834477-dbab4500-a447-11eb-908a-139a6edaec5c.gif">

<h2 align="center">─「 ✨ ғᴇᴀᴛᴜʀᴇs 」─</h2>

- `/play`, `/vplay` from a song name, YouTube link, playlist, or a reply to an audio/video file
- Queue, pause, resume, skip, stop, seek, loop and replay controls, with inline buttons
- Authorized users, blacklist, sudo users, per-chat settings (play mode, command delete, language)
- 13 interface languages (`/lang`)
- Up to **3 assistant accounts** (string sessions); each chat is bound to one of them
- Every assistant joins the support group and updates channel on start
- All settings are stored in **MongoDB** and survive restarts and redeploys
- Logger group: `Bot Started`, `Assistant started`, new chats and users are reported there
- Downloads through your own **download API** (`API_URL` + `API_KEY`) or with `yt-dlp` and cookies
- Crash reporter: a fatal signal prints a backtrace to the logs

<h2 align="center">─「 🦋 ᴅᴇᴘʟᴏʏ 💗 」─</h2>

<p align="center">
  <a href="https://dashboard.heroku.com/new?template=https://github.com/Badmunda05/NoxBeats"><img src="https://img.shields.io/badge/Deploy%20to-Heroku-430098?style=for-the-badge&logo=heroku&logoColor=white" alt="Deploy to Heroku"></a>
  <a href="https://app.koyeb.com/deploy?type=git&repository=github.com/Badmunda05/NoxBeats&branch=main&name=noxbeats"><img src="https://img.shields.io/badge/Deploy%20to-Koyeb-121212?style=for-the-badge&logo=koyeb&logoColor=white" alt="Deploy to Koyeb"></a>
  <a href="https://render.com/deploy?repo=https://github.com/Badmunda05/NoxBeats"><img src="https://img.shields.io/badge/Deploy%20to-Render-46E3B7?style=for-the-badge&logo=render&logoColor=white" alt="Deploy to Render"></a>
  <a href="https://railway.app/new/template?template=https://github.com/Badmunda05/NoxBeats"><img src="https://img.shields.io/badge/Deploy%20to-Railway-0B0D0E?style=for-the-badge&logo=railway&logoColor=white" alt="Deploy to Railway"></a>
</p>

On Heroku set the stack to **container** (`heroku stack:set container -a <your-app>`), add the config vars (see Config Vars below) and scale the worker (`heroku ps:scale worker=1`). Add the bot **and** the assistant account to your log group (`LOGGER_ID`) and promote the bot to admin.

<h2 align="center">─「 🦋 ʜᴏsᴛ ᴏɴ ᴠᴘs 💗 」─</h2>

**Docker**

```bash
git clone https://github.com/Badmunda05/NoxBeats && cd NoxBeats
docker build -t noxbeats .
docker run -d --name noxbeats --restart unless-stopped --env-file .env noxbeats
docker logs -f noxbeats
```

**Build from source** (Ubuntu 24.04)

```bash
sudo apt-get install -y g++ cmake make curl unzip ffmpeg \
    libcurl4-openssl-dev zlib1g-dev libmongoc-dev nlohmann-json3-dev pkg-config
sudo curl -fsSL https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp_linux -o /usr/local/bin/yt-dlp
sudo chmod +x /usr/local/bin/yt-dlp      # standalone binary, no Python needed

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

<h2 align="center">─「 🦋 ᴄᴏɴғɪɢ ᴠᴀʀs 💗 」─</h2>

| Variable | Required | Description |
| :-- | :-: | :-- |
| `API_ID` | ✅ | App id from [my.telegram.org](https://my.telegram.org) |
| `API_HASH` | ✅ | App hash from [my.telegram.org](https://my.telegram.org) |
| `BOT_TOKEN` | ✅ | Bot token from [@BotFather](https://t.me/BotFather) |
| `MONGO_DB_URI` | ✅ | MongoDB connection string (a free [MongoDB Atlas](https://www.mongodb.com/atlas) cluster is enough) |
| `SESSION` | ✅ | Assistant string session (Pyrogram v2 or Telethon) |
| `LOGGER_ID` | ✅ | Log group id (bot must be admin there) |
| `OWNER_ID` | ✅ | Your Telegram user id |
| `SESSION2`, `SESSION3` | – | Extra assistant sessions |
| `SESSION_TYPE` | – | `pyrogram` (default) or `telethon` |
| `API_URL` | – | Download API base URL (for example `https://api.yourdomain.site`) |
| `API_KEY` | – | Download API key. DM [@BadMundaXD](https://t.me/BadMundaXD) for a key, or use your own API |
| `COOKIES_URL` | – | batbin.me links to cookie files for `yt-dlp` |
| `YTDLP_UPDATE` | – | Run `yt-dlp -U` once at start (default `True`) |
| `DURATION_LIMIT` | – | Longest playable track in minutes (default `60`) |
| `QUEUE_LIMIT` | – | Max tracks per chat queue (default `20`) |
| `PLAYLIST_LIMIT` | – | Max tracks taken from a playlist (default `20`) |
| `AUTO_LEAVE` | – | Assistant leaves idle chats hourly (`True` / `False`) |
| `AUTO_END` | – | End the stream when the voice chat is empty |
| `THUMB_GEN` | – | Generate track thumbnails |
| `VIDEO_PLAY` | – | Allow `/vplay` |
| `LANG_CODE` | – | Default language (`en`, `hi`, `pa`, …) |
| `SUPPORT_CHANNEL` | – | Updates channel (default `https://t.me/PBX_UPDATE`) |
| `SUPPORT_CHAT` | – | Support group (default `https://t.me/PBXCHATS`) |
| `SOURCE_URL` | – | Link behind the **Source** button |

**Cookies or API:** use only `COOKIES_URL` (yt-dlp with cookies), or point `API_URL` + `API_KEY` at any download API. The bot calls `GET <API_URL>/download?url=<video_id>&type=audio|video&api_key=<API_KEY>` and expects the media file back. For an API with a different route or response, adapt `download_via_api()` in `Bad/platforms/yt.cpp`.

**yt-dlp.** The Docker image ships the standalone `yt-dlp` binary, so Python is not installed. Search uses YouTube's InnerTube API first, downloads try your API first, and `yt-dlp` is only the fallback. YouTube changes often: the bot checks for a newer `yt-dlp` at start (`YTDLP_UPDATE`), and you can pin a version at build time with `--build-arg YTDLP_VERSION=<release tag>`. YouTube often asks datacenter IPs to prove they are not bots, so set `COOKIES_URL` or `API_URL` + `API_KEY`, otherwise downloads can fail.

Sudo users, blacklist, authorized users, languages and chat/user lists are saved in the `state` collection of your MongoDB database. `MongoDB connected successfully.` is logged only after a real connection and ping; if the database is unreachable the bot stops with an error.

<h2 align="center">─「 🦋 ᴄᴏᴍᴍᴀɴᴅs 💗 」─</h2>

**Everyone**

| Command | What it does |
| :-- | :-- |
| `/play <name / link / reply>` | Play audio in the voice chat (`-f` force play, `-v` video) |
| `/vplay <name / link / reply>` | Play video |
| `/queue` | Show the queue |
| `/ping` | Latency, uptime, CPU, RAM, disk |
| `/stats` | Bot statistics |
| `/lang` | Change language |
| `/help`, `/start` | Menus |
| `/sudolist` | List sudo users |

**Admins / authorized users**

| Command | What it does |
| :-- | :-- |
| `/pause`, `/resume` | Pause or resume |
| `/skip` | Next track |
| `/stop`, `/end` | Stop and clear the queue |
| `/seek <s>`, `/seekback <s>` | Jump forward or back |
| `/loop` | Repeat the current track |
| `/reload` | Refresh the admin cache |
| `/auth`, `/unauth`, `/authlist` | Let a user control playback without being admin |
| `/playmode`, `/settings` | Chat settings |

**Sudo / owner**

| Command | What it does |
| :-- | :-- |
| `/ac`, `/activevc` | Active voice chats |
| `/broadcast [-nochat] [-user] [-copy]` | Broadcast a replied message |
| `/blacklist`, `/unblacklist` | Block chats or users |
| `/logs`, `/logger [on\|off]` | Log file and logger group switch |
| `/restart` | Clear caches and restart |
| `/addsudo`, `/rmsudo` | Manage sudo users (owner) |
| `/eval <shell command>` (also `/sh`) | Run a shell command (owner only) |

<h2 align="center">─「 🦋 ᴛʀᴏᴜʙʟᴇsʜᴏᴏᴛɪɴɢ 💗 」─</h2>

| Symptom | Check |
| :-- | :-- |
| `MongoDB connection failed` | `MONGO_DB_URI` is wrong, or the cluster blocks your host (Atlas → Network Access → allow `0.0.0.0/0`) |
| `assistant 1: [406] AUTH_KEY_DUPLICATED` | The same `SESSION` runs somewhere else. Use a fresh string session for this bot |
| `AUTH_KEY_UNREGISTERED` / `SESSION_REVOKED` | The session was logged out; generate a new one |
| `Failed to process the query` | Search failed: look for `YouTube search request failed` or `yt-dlp search failed` in the logs |
| Track found but no sound / no join | The assistant must be able to see the group, and a voice chat must be **started** |
| `API download failed` / `returned no media` | `API_URL` / `API_KEY` wrong; the bot falls back to `yt-dlp` (needs cookies on datacenter IPs) |
| `Assistant started` missing in the log group | The assistant account is not a member of `LOGGER_ID` |
| Crash | The logs show `*** CRASH: SIG… ***` with a backtrace; send those lines |

<h2 align="center">─「 🦋 ᴄʀᴇᴅɪᴛs 💗 」─</h2>

<p align="center">
  <a href="https://t.me/Badmundaxd"><img src="https://img.shields.io/badge/Made%20by-Badmunda-2CA5E0?style=for-the-badge&logo=telegram&logoColor=white" alt="Made by Badmunda"></a>
  <a href="https://github.com/Badmunda05/NoxBeats/blob/main/LICENSE"><img src="https://img.shields.io/badge/License-LICENSE-blue?style=for-the-badge" alt="License"></a>
</p>

<p align="center">
  <a href="https://github.com/pytgcalls/ntgcalls"><img src="https://img.shields.io/badge/Built%20with-ntgcalls-8A2BE2?style=for-the-badge" alt="ntgcalls"></a>
  <a href="https://github.com/LonamiWebs/Telethon"><img src="https://img.shields.io/badge/Built%20with-Telethon%20schema-2CA5E0?style=for-the-badge" alt="Telethon"></a>
  <a href="https://github.com/yt-dlp/yt-dlp"><img src="https://img.shields.io/badge/Built%20with-yt--dlp-FF0000?style=for-the-badge" alt="yt-dlp"></a>
  <a href="https://ffmpeg.org"><img src="https://img.shields.io/badge/Built%20with-FFmpeg-007808?style=for-the-badge" alt="FFmpeg"></a>
  <a href="https://github.com/nlohmann/json"><img src="https://img.shields.io/badge/Built%20with-nlohmann%2Fjson-333333?style=for-the-badge" alt="nlohmann/json"></a>
  <a href="https://github.com/mongodb/mongo-c-driver"><img src="https://img.shields.io/badge/Built%20with-libmongoc-47A248?style=for-the-badge" alt="libmongoc"></a>
</p>

<h2 align="center">─「 🦋 ᴄᴏɴᴛᴀᴄᴛ 💗 」─</h2>

<p align="center">
  <a href="https://t.me/PBXCHATS"><img src="https://img.shields.io/badge/Support%20Group-Join-2CA5E0?style=for-the-badge&logo=telegram&logoColor=white" alt="Support Group"></a>
  <a href="https://t.me/PBX_UPDATE"><img src="https://img.shields.io/badge/Updates%20Channel-Join-2CA5E0?style=for-the-badge&logo=telegram&logoColor=white" alt="Updates Channel"></a>
  <a href="mailto:munda.bad1322@gmail.com"><img src="https://img.shields.io/badge/Contact%20Email-Mail-D14836?style=for-the-badge&logo=gmail&logoColor=white" alt="Contact Email"></a>
  <a href="https://t.me/Badmundaxd"><img src="https://img.shields.io/badge/Contact%20Owner-Message-2CA5E0?style=for-the-badge&logo=telegram&logoColor=white" alt="Contact Owner"></a>
</p>
