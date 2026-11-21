#pragma once

#include <cstddef>

namespace ecosim {

/// Per-thread last-error message.
///
/// Per *thread*, not per process: each world owns its own lock, so two worlds
/// on two threads can legitimately be inside the engine at the same time. A
/// process-wide buffer did get corrupted by exactly that (xUnit runs test
/// classes in parallel), which showed up as an intermittent AccessViolation in
/// the test host rather than as a wrong message.
///
/// A POD thread_local needs no destructor registration, which matters here:
/// this toolchain's static runtime does not provide __cxa_thread_atexit that a
/// thread_local std::string requires.
constexpr size_t kErrorCapacity = 512;

void set_error(const char* msg);
void clear_error();

/// Borrowed pointer to this thread's buffer, valid until the next
/// set_error/clear_error on the same thread.
const char* current_error();

}  // namespace ecosim
