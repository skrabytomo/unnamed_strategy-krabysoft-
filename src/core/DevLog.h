#pragma once
#include <vector>
#include <string>
#include <cstdarg>

// Global in-game developer log.
// Call gLog() anywhere in game code instead of printf().
// The log is viewable in-game via ESC → Debug → Game Log.

static constexpr int DEV_LOG_CAPACITY = 400;

// Printf-compatible logging function. Writes to stdout AND the in-game buffer.
void gLog(const char* fmt, ...) noexcept;

namespace DevLog {
    // Returns a COPY, taken under the lock.
    //
    // This used to hand back a reference to the shared vector while gLog()
    // appended to it under a mutex. Single-threaded that was merely sloppy;
    // the moment AI work runs off the render thread it is a crash — gLog()
    // from a worker can reallocate the vector while the UI is walking it.
    // Copying is cheap here: only the debug log panel reads it, only while
    // that panel is open, and the buffer is capped at DEV_LOG_CAPACITY.
    std::vector<std::string> snapshot();
    void clear();
    bool hasNewLines();   // true once new lines were added since last check
    void markSeen();
    void setSilent(bool silent); // suppress stdout output (useful for headless sims)
}
