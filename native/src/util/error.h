#pragma once

#include <string>

namespace ecosim {

// Thread-local last error message, handed out as a stable UTF-8 C string by the
// ABI layer. Set on every failing call; cleared on every successful one.
void set_error(const char* msg);
void clear_error();

// Borrowed reference, valid until the next set_error/clear_error on this thread.
const std::string& current_error();

}  // namespace ecosim
