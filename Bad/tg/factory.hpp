// Adapter factories. factory_real.cpp wires the Bot API client + ntgcalls bridge.
#pragma once
#include <memory>
#include "client.hpp"
#include "../voice/backend.hpp"
namespace bot {
std::unique_ptr<tg::Client> create_tg_client();           // starts the assistant accounts, then the Bot API client
std::unique_ptr<voice::Backend> create_voice_backend(tg::Client& c);
void shutdown_adapters();                                  // logs the assistants out of their voice chats
}
