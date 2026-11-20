#include "util/error.h"

#include <string>

namespace ecosim {

namespace {

// thread_local: two consumers (the app and the CLI) may step different worlds
// on different threads and must not read each other's diagnostics.
thread_local std::string g_message;

}  // namespace

void set_error(const char* msg) { g_message = msg ? msg : "unknown error"; }

void clear_error() { g_message.clear(); }

const std::string& current_error() { return g_message; }

}  // namespace ecosim
