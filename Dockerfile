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
RUN apt-get update && apt-get install -y --no-install-recommends \
        ffmpeg curl ca-certificates libcurl4 libssl3 zlib1g libmongoc-1.0-0t64 python3 \
    && curl -fsSL https://github.com/yt-dlp/yt-dlp/releases/latest/download/yt-dlp -o /usr/local/bin/yt-dlp \
    && chmod +x /usr/local/bin/yt-dlp && rm -rf /var/lib/apt/lists/*
WORKDIR /app
COPY --from=build /src/build/cpp-bot /app/cpp-bot
COPY --from=build /src/third_party/ntgcalls/lib/libntgcalls.so /app/lib/libntgcalls.so
COPY strings /app/strings
COPY Bad/telegram_schema /app/telegram_schema
CMD ["/app/cpp-bot"]
