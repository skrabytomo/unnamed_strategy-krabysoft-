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
    const std::vector<std::string>& lines();
    void clear();
    bool hasNewLines();   // true once new lines were added since last check
    void markSeen();
}
