# Security Policy

## Reporting a vulnerability

Please **do not open a public issue** for a security problem. Report it privately:

- Telegram: [@Badmundaxd](https://t.me/Badmundaxd)
- Email: [munda.bad1322@gmail.com](mailto:munda.bad1322@gmail.com)

Include what you found, the steps to reproduce it, and the version or commit you tested. You will get a reply as soon as possible, and the fix will be published before the details are shared.

## Supported versions

Only the latest commit on the `main` branch is supported. Please update before reporting.

## Keeping your deployment safe

- **Secrets:** `BOT_TOKEN`, `SESSION`, `SESSION2` … `SESSION10`, `MONGO_DB_URI`, `API_HASH` and `API_KEY` give full access. Keep them in environment variables or a `.env` file, and never commit them. If one leaks, rotate it right away (revoke the bot token with @BotFather, terminate the session in Telegram, change the database password).
- **One session, one bot:** never run the same assistant `SESSION` in two places.
- **Owner commands:** `/eval` runs shell commands on your server. Set `OWNER_ID` to your own account only, and add sudo users only if you trust them completely.
- **MongoDB:** use a dedicated database user with a strong password. Allow only the IP addresses that need access when your host has a fixed IP.
- **Logs:** `log.txt` and the logger group can contain chat names and user ids. Keep the logger group private.
- **Updates:** rebuild from time to time so that `yt-dlp`, `ffmpeg` and the system packages stay current.
