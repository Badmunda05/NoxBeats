#include "backend.hpp"
namespace bot::voice {
static Backend* g = nullptr;
Backend& backend() { return *g; }
void set_backend(Backend* b) { g = b; }
}
