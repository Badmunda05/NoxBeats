<h1 align="center">🎧 NØX BEATS</h1>

<p align="center">
  <img src="https://files.catbox.moe/z5febs.png" alt="NØX BEATS" width="520">
</p>

<p align="center">
  <b>A Telegram voice-chat music bot built with C++20.</b><br>
  Powered by ntgcalls, MongoDB, FFmpeg and yt-dlp.
</p>

<p align="center">
  <img src="https://img.shields.io/badge/C%2B%2B-20-00599C?logo=cplusplus" alt="C++20">
  <img src="https://img.shields.io/badge/Voice-ntgcalls-8A2BE2" alt="ntgcalls">
  <img src="https://img.shields.io/badge/Database-MongoDB-47A248?logo=mongodb&logoColor=white" alt="MongoDB">
</p>

## Features

- Play audio or video from a song name, link, playlist, or replied media.
- Queue, pause, resume, skip, stop, seek and loop controls.
- Autoplay (related songs when the queue ends) and a per-chat thumbnail switch.
- Personal playlists (`/addplaylist`, `/playlist`, `/rmplaylist`, `/playplaylist`).
- Spotify, Apple Music, JioSaavn and SoundCloud links, m3u8 streams, channel play (`/cplay`), speed/jump/mute, queue tools and RTMP streaming.
- Admin tools, authorized users and per-chat settings.
- Up to 10 assistant accounts (`SESSION` … `SESSION10`) to spread the voice chats.
- Multiple interface languages.
- MongoDB-backed settings that persist across restarts.
- Optional download API or yt-dlp support.

## Commands

| Command | Description |
| --- | --- |
| `/play <name/link>` | Play audio |
| `/vplay <name/link>` | Play video |
| `/queue` | Show the queue |
| `/pause` / `/resume` | Pause or resume playback |
| `/skip` | Skip the current track |
| `/stop` | Stop playback and clear the queue |
| `/loop` | Toggle track looping |
| `/autoplay` | Autoplay on/off, language and mood |
| `/thumbnail` | Thumbnail on/off |
| `/lang` | Change language |
| `/help` / `/start` | Open the help menu |

Some commands are restricted to admins, authorized users, or the bot owner.

<p align="center">
  <a href="COMMANDS.md"><img src="https://img.shields.io/badge/Full%20Command%20List-COMMANDS.md-FF0000?style=for-the-badge" alt="Full command list"></a>
  <a href="MORE.md"><img src="https://img.shields.io/badge/More%20Details-MORE.md-181717?style=for-the-badge" alt="More details"></a>
</p>

## Deployment

Deploy using one of the supported platforms:

<p align="center">
  <a href="https://dashboard.heroku.com/new?template=https://github.com/Badmunda05/NoxBeats"><img src="https://img.shields.io/badge/Deploy%20to-Heroku-430098?style=for-the-badge&logo=heroku&logoColor=white" alt="Deploy to Heroku"></a>
  <a href="https://app.koyeb.com/deploy?type=git&repository=github.com/Badmunda05/NoxBeats&branch=main&name=noxbeats"><img src="https://img.shields.io/badge/Deploy%20to-Koyeb-121212?style=for-the-badge&logo=koyeb&logoColor=white" alt="Deploy to Koyeb"></a>
  <a href="https://render.com/deploy?repo=https://github.com/Badmunda05/NoxBeats"><img src="https://img.shields.io/badge/Deploy%20to-Render-46E3B7?style=for-the-badge&logo=render&logoColor=white" alt="Deploy to Render"></a>
  <a href="https://railway.app/new/template?template=https://github.com/Badmunda05/NoxBeats"><img src="https://img.shields.io/badge/Deploy%20to-Railway-0B0D0E?style=for-the-badge&logo=railway&logoColor=white" alt="Deploy to Railway"></a>
</p>

For VPS deployment, use Docker:

```bash
git clone https://github.com/Badmunda05/NoxBeats
cd NoxBeats
docker build -t noxbeats .
docker run -d --name noxbeats --restart unless-stopped --env-file .env noxbeats
```

Create a `.env` file with the required configuration before starting the bot.

## Configuration

Required environment variables:

| Variable | Purpose |
| --- | --- |
| `API_ID` | Telegram API ID |
| `API_HASH` | Telegram API hash |
| `BOT_TOKEN` | Bot token from [@BotFather](https://t.me/BotFather) |
| `SESSION` | Assistant account string session |
| `LOGGER_ID` | Log group ID |
| `OWNER_ID` | Bot owner's Telegram user ID |
| `MONGO_DB_URI` | MongoDB connection URI |

Optional variables include `SESSION2` … `SESSION10`, `SESSION_TYPE`, `API_URL`, `API_KEY`, `COOKIES_URL`, `USE_COOKIES`, `SPOTIFY_CLIENT_ID`, `SPOTIFY_CLIENT_SECRET`, `DURATION_LIMIT`, `QUEUE_LIMIT`, `PLAYLIST_LIMIT`, `AUTO_LEAVE`, `AUTO_END`, `THUMB_GEN`, `VIDEO_PLAY`, `LANG_CODE`, `SUPPORT_CHANNEL`, `SUPPORT_CHAT` and `SOURCE_URL`.

Keep credentials and session strings private. Never commit your `.env` file.

## Troubleshooting

- **MongoDB connection failed:** Check `MONGO_DB_URI` and your database network access.
- **Assistant authentication error:** Make sure the session is valid and isn't being used by another running instance.
- **No audio or voice-chat join:** Ensure the assistant can access the group and the voice chat is active.
- **Download failed:** Check the download API settings or yt-dlp configuration and logs.

## Credits

<p align="center">
  <a href="https://github.com/pytgcalls/ntgcalls"><img src="https://img.shields.io/badge/Built%20with-ntgcalls-8A2BE2?style=for-the-badge" alt="ntgcalls"></a>
  <a href="https://github.com/yt-dlp/yt-dlp"><img src="https://img.shields.io/badge/Built%20with-yt--dlp-FF0000?style=for-the-badge" alt="yt-dlp"></a>
  <a href="https://ffmpeg.org"><img src="https://img.shields.io/badge/Built%20with-FFmpeg-007808?style=for-the-badge" alt="FFmpeg"></a>
  <a href="https://www.mongodb.com/"><img src="https://img.shields.io/badge/Built%20with-MongoDB-47A248?style=for-the-badge" alt="MongoDB"></a>
  <a href="https://github.com/nlohmann/json"><img src="https://img.shields.io/badge/Built%20with-nlohmann%2Fjson-333333?style=for-the-badge" alt="nlohmann/json"></a>
</p>

## Links

<p align="center">
  <a href="https://github.com/Badmunda05/NoxBeats"><img src="https://img.shields.io/badge/Source-GitHub-181717?style=for-the-badge&logo=github&logoColor=white" alt="Source"></a>
  <a href="https://t.me/PBXCHATS"><img src="https://img.shields.io/badge/Support-Telegram%20Group-2CA5E0?style=for-the-badge&logo=telegram&logoColor=white" alt="Support"></a>
  <a href="https://t.me/PBX_UPDATE"><img src="https://img.shields.io/badge/Updates-Telegram%20Channel-2CA5E0?style=for-the-badge&logo=telegram&logoColor=white" alt="Updates"></a>
  <a href="https://t.me/Badmundaxd"><img src="https://img.shields.io/badge/Owner-Telegram-2CA5E0?style=for-the-badge&logo=telegram&logoColor=white" alt="Owner"></a>
</p>

## License

<p align="center">
  <a href="https://github.com/Badmunda05/NoxBeats/blob/main/LICENSE"><img src="https://img.shields.io/badge/License-View%20LICENSE-blue?style=for-the-badge" alt="License"></a>
</p>
