#include "DevLog.h"
#include <cstdio>
#include <cstring>
#include <mutex>

static std::vector<std::string> s_lines;
static bool s_hasNew = false;
static std::mutex s_mtx;

void gLog(const char* fmt, ...) noexcept
{
    char buf[1024];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    // Still emit to stdout so terminal/CI can capture it
    fputs(buf, stdout);

    // Strip trailing newline for clean ImGui display
    int len = static_cast<int>(strlen(buf));
    while (len > 0 && (buf[len-1] == '\n' || buf[len-1] == '\r'))
        buf[--len] = '\0';

    if (len == 0) return;

    std::lock_guard<std::mutex> lk(s_mtx);
    if (static_cast<int>(s_lines.size()) >= DEV_LOG_CAPACITY)
        s_lines.erase(s_lines.begin(), s_lines.begin() + 50); // drop oldest 50
    s_lines.emplace_back(buf);
    s_hasNew = true;
}

const std::vector<std::string>& DevLog::lines()
{
    return s_lines;
}

void DevLog::clear()
{
    std::lock_guard<std::mutex> lk(s_mtx);
    s_lines.clear();
    s_hasNew = false;
}

bool DevLog::hasNewLines()
{
    std::lock_guard<std::mutex> lk(s_mtx);
    return s_hasNew;
}

void DevLog::markSeen()
{
    std::lock_guard<std::mutex> lk(s_mtx);
    s_hasNew = false;
}
