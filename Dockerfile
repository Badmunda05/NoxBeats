# ---- build (C++ bot + libntgcalls shared library) ----
FROM ubuntu:24.04 AS build
RUN apt-get update && apt-get install -y --no-install-recommends \
        g++ cmake make curl unzip ca-certificates libcurl4-openssl-dev zlib1g-dev libmongoc-dev nlohmann-json3-dev pkg-config \
    && rm -rf /var/lib/apt/lists/*
ARG NTG_VERSION=v2.1.0
WORKDIR /src
COPY . .
RUN ARCH=$(uname -m); \
    if [ "$ARCH" = "x86_64" ]; then NTG=x86_64; else NTG=arm64; fi; \
    mkdir -p third_party/ntgcalls/lib third_party/ntgcalls/include /tmp/ntg && \
    curl -fsSL "https://github.com/pytgcalls/ntgcalls/releases/download/${NTG_VERSION}/ntgcalls.linux-${NTG}-shared_libs.zip" -o /tmp/ntg.zip && \
    unzip -q /tmp/ntg.zip -d /tmp/ntg && \
    cp "$(find /tmp/ntg -name libntgcalls.so | head -n1)" third_party/ntgcalls/lib/ && \
    cp "$(find /tmp/ntg -name ntgcalls.h | head -n1)" third_party/ntgcalls/include/ && rm -rf /tmp/ntg /tmp/ntg.zip
RUN cmake -B build -DCMAKE_BUILD_TYPE=Release -DWITH_NTGCALLS=ON && cmake --build build -j2 --target cpp-bot

# ---- runtime ----
FROM ubuntu:24.04
# yt-dlp: the standalone release binary (no Python needed). Set YTDLP_VERSION to a release tag (for example 2026.08.19)
# to pin it; the bot also runs `yt-dlp -U` at start (disable with YTDLP_UPDATE=False).
ARG YTDLP_VERSION=latest
RUN apt-get update && apt-get install -y --no-install-recommends \
        ffmpeg curl ca-certificates libcurl4 libssl3 zlib1g libmongoc-1.0-0t64 \
    && if [ "$(uname -m)" = "x86_64" ]; then YT=yt-dlp_linux; else YT=yt-dlp_linux_aarch64; fi \
    && if [ "$YTDLP_VERSION" = "latest" ]; then URL="https://github.com/yt-dlp/yt-dlp/releases/latest/download/$YT"; \
       else URL="https://github.com/yt-dlp/yt-dlp/releases/download/$YTDLP_VERSION/$YT"; fi \
    && curl -fsSL "$URL" -o /usr/local/bin/yt-dlp \
    && chmod +x /usr/local/bin/yt-dlp && yt-dlp --version \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /app
COPY --from=build /src/build/cpp-bot /app/cpp-bot
COPY --from=build /src/third_party/ntgcalls/lib/libntgcalls.so /app/lib/libntgcalls.so
COPY strings /app/strings
COPY Bad/telegram_schema /app/telegram_schema
CMD ["/app/cpp-bot"]
