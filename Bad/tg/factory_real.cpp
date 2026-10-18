#include "factory.hpp"
#include <stdexcept>
#include "../core/config.hpp"
#include "../voice/assistant.hpp"
#include "botapi_client.hpp"

namespace bot {
static std::unique_ptr<voice::AssistantManager> g_mgr;

std::unique_ptr<tg::Client> create_tg_client() {
    const Config& c = config();
    voice::AssistantOptions o;
    o.sessions = c.SESSIONS;
    o.session_type = c.SESSION_TYPE;
    o.api_id = c.API_ID;
    g_mgr = std::make_unique<voice::AssistantManager>(o);
    std::string err;
    if (!g_mgr->start(&err)) { g_mgr.reset(); throw std::runtime_error(err); }

    auto client = std::make_unique<tg::BotApiClient>(c.BOT_TOKEN, c.BOT_API_URL, g_mgr.get());
    if (!client->init(&err)) throw std::runtime_error("Bot API getMe failed (wrong BOT_TOKEN?): " + err);
    return client;
}

std::unique_ptr<voice::Backend> create_voice_backend(tg::Client&) {
    if (!g_mgr) throw std::runtime_error("assistants not started");
    return std::make_unique<voice::AssistantBackend>(*g_mgr);
}

void shutdown_adapters() { if (g_mgr) g_mgr->stop(); }
}  // namespace bot
