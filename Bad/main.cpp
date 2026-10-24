// Boot sequence: config, database, Telegram client, assistants, modules, background tasks.
#include <csignal>
#include <cstdio>
#include <cstring>
#include <execinfo.h>
#include <ctime>
#include <filesystem>
#include <iostream>
#include <memory>
#include <thread>
#include <unistd.h>
#include "cmd/common.hpp"
#include "cmd/register.hpp"
#include "core/app.hpp"
#include "core/calls.hpp"
#include "core/config.hpp"
#include "core/log.hpp"
#include "core/pool.hpp"
#include "core/util.hpp"
#include "db/db.hpp"
#include "i18n/lang.hpp"
#include "platforms/yt.hpp"
#include "tg/factory.hpp"
#include "tg/mock_client.hpp"
#include "voice/mock_backend.hpp"

using namespace bot;

static void on_signal(int) { request_stop(false); }

// Crash reporter: prints the signal and a backtrace to stderr (shows up in `heroku logs`), then dies as usual.
static void on_crash(int sig) {
    const char* name = sig == SIGSEGV ? "SIGSEGV" : sig == SIGABRT ? "SIGABRT" : sig == SIGBUS ? "SIGBUS" : sig == SIGFPE ? "SIGFPE" : "signal";
    char head[64];
    int n = std::snprintf(head, sizeof head, "\n*** CRASH: %s ***\n", name);
    if (n > 0) (void)!write(STDERR_FILENO, head, static_cast<size_t>(n));
    void* frames[48];
    int cnt = backtrace(frames, 48);
    backtrace_symbols_fd(frames, cnt, STDERR_FILENO);
    signal(sig, SIG_DFL);
    raise(sig);
}

int main(int argc, char** argv) {
    for (int sg : {SIGSEGV, SIGABRT, SIGBUS, SIGFPE}) signal(sg, on_crash);
    const bool mock = std::getenv("BOT_MOCK") != nullptr;
    config() = Config::load();
    if (auto miss = config().check(!mock); !miss.empty() && !mock) {
        std::cerr << "Missing required environment variables: " << util::join(miss, ", ") << "\n";
        return 1;
    }

    std::filesystem::create_directories("downloads");
    std::filesystem::create_directories("cache");
    std::filesystem::create_directories("cookies");
    init_language("strings");
    init_db(config().DB_PATH, mock ? std::string() : config().MONGO_DB_URI);

    std::unique_ptr<tg::Client> client;
    std::unique_ptr<voice::Backend> backend;
    try {
        if (mock) {
            auto mc = std::make_unique<tg::MockClient>();
            mc->admin_ids[-1001] = {config().OWNER_ID ? config().OWNER_ID : 1};
            client = std::move(mc);
            backend = std::make_unique<voice::MockBackend>();
        } else {
            client = create_tg_client();
            backend = create_voice_backend(*client);
        }
    } catch (const std::exception& e) {
        std::cerr << e.what() << "\n";
        return 1;
    }
    set_app_client(client.get());
    tg::set_client(client.get());
    voice::set_backend(backend.get());

    try { db().connect(); } catch (const std::exception& e) { std::cerr << e.what() << "\n"; return 1; }

    // Bot.boot(): the bot must be able to post in (and be admin of) the log group
    if (!mock) {
        try {
            client->send_message(config().LOGGER_ID, "ʙᴏᴛ ꜱᴛᴀʀᴛᴇᴅ");
            auto st = client->member_status(config().LOGGER_ID, client->me().id);
            if (st != tg::MemberStatus::Admin && st != tg::MemberStatus::Owner) {
                std::cerr << "Please promote the bot as an admin in logger group.\n";
                return 1;
            }
        } catch (const std::exception& e) {
            std::cerr << "Bot has failed to access the log group: " << config().LOGGER_ID << "\nReason: " << e.what() << "\n";
            return 1;
        }
    }
    log::info("Bot started as @" + client->me().username);
    std::thread joiner, updater;
    if (!mock) {
        for (int i = 1; i <= client->assistants(); ++i)
            client->assistant_send(i, config().LOGGER_ID, client->assistants() > 1 ? "Assistant " + std::to_string(i) + " started" : "ᴀꜱꜱɪꜱᴛᴀɴᴛ ꜱᴛᴀʀᴛᴇᴅ");
        // keep yt-dlp current (YouTube changes often); needs write access to the yt-dlp binary, otherwise it only logs
        if (config().YTDLP_UPDATE) {
            updater = std::thread([] {
                auto r = util::exec({yt().yt_dlp, "-U"}, 120);
                std::string out = util::trim(r.out + r.err);
                size_t nl = out.find_last_of('\n');
                if (nl != std::string::npos) out = out.substr(nl + 1);
                if (r.code == 0) log::info("yt-dlp update check: " + out);
                else log::warn("yt-dlp update check failed (exit " + std::to_string(r.code) + "): " + out);
            });
        }
        // every assistant joins the support group and the updates channel (SUPPORT_CHAT / SUPPORT_CHANNEL)
        joiner = std::thread([c = client.get()] {
            for (int i = 1; i <= c->assistants(); ++i) {
                for (const std::string& link : {config().SUPPORT_CHAT, config().SUPPORT_CHANNEL}) {
                    if (link.empty()) continue;
                    try {
                        c->assistant_join(i, link);
                        log::info("Assistant " + std::to_string(i) + " joined " + link);
                    } catch (const tg::Error& e) {
                        if (e.kind == tg::Err::UserAlreadyParticipant) log::info("Assistant " + std::to_string(i) + " is already in " + link);
                        else log::warn("Assistant " + std::to_string(i) + " could not join " + link + ": " + e.what());
                    } catch (const std::exception& e) {
                        log::warn("Assistant " + std::to_string(i) + " could not join " + link + ": " + e.what());
                    }
                }
            }
        });
    }

    bad().boot();
    register_all();
    log::info("Loaded " + std::to_string(module_count()) + " modules.");
    if (config().USE_COOKIES && !config().COOKIES_URL.empty()) yt().save_cookies(config().COOKIES_URL);
    load_runtime_sets();
    log::info("Loaded " + std::to_string(sudo_count()) + " sudo users.");
    start_background_tasks();

    ThreadPool pool(16);
    client->on_message = [&](const Message& m) { pool.submit([m] { dispatch_message(m); }); };
    client->on_bot_member = [&](const BotMemberChange& c) { pool.submit([c] { handle_bot_member(c); }); };
    client->on_callback = [&](const CallbackQuery& q) { pool.submit([q] { dispatch_callback(q); }); };

    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);

    std::thread repl;
    if (mock) {
        // BOT_MOCK=1: type "<chat_id> <user_id> <text>" to simulate group messages.
        auto* mc = static_cast<tg::MockClient*>(client.get());
        mc->users[1] = User{1, "Tester", "tester", false};
        repl = std::thread([&, mc] {
            std::string line;
            size_t shown = 0;
            while (std::getline(std::cin, line)) {
                auto p = util::split_ws(line);
                if (p.size() < 3) continue;
                Message m;
                m.id = 1;
                m.chat.id = std::stoll(p[0]);
                m.chat.type = m.chat.id < 0 ? ChatType::Supergroup : ChatType::Private;
                m.chat.title = "Mock Group";
                m.from = User{std::stoll(p[1]), "User" + p[1], "", false};
                m.text = util::join(p, " ", 2);
                mc->inject(m);
                pool.drain();
                auto ev = mc->snapshot();
                for (; shown < ev.size(); ++shown)
                    std::cout << "<- [" << ev[shown].kind << " " << ev[shown].chat << "] " << ev[shown].text << "\n";
            }
            request_stop(false);
        });
    }

    client->run();   // blocks until stop

    log::info("Stopping...");
    stop_background_tasks();
    pool.drain();
    db().close();
    if (repl.joinable()) repl.join();
    if (joiner.joinable()) joiner.join();
    if (updater.joinable()) updater.join();
    shutdown_adapters();
    log::info("Stopped.");
    if (restart_requested()) execv("/proc/self/exe", argv);
    return 0;
}
