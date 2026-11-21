#include "util/error.h"

#include <cstdio>
#include <cstring>

namespace ecosim {

namespace {

// POD thread_local: no constructor, no destructor, therefore no
// __cxa_thread_atexit and no dependency on a shared runtime helper.
thread_local char g_message[kErrorCapacity];
thread_local bool g_present = false;

}  // namespace

void set_error(const char* msg) {
  if (msg == nullptr) {
    g_message[0] = '\0';
    g_present = false;
    return;
  }
  std::snprintf(g_message, kErrorCapacity, "%s", msg);
  g_present = true;
}

void clear_error() {
  g_message[0] = '\0';
  g_present = false;
}

const char* current_error() { return g_present ? g_message : ""; }

}  // namespace ecosim
