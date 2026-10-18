#include "client.hpp"
namespace bot::tg {
static Client* g = nullptr;
Client& client() { return *g; }
void set_client(Client* c) { g = c; }
}
