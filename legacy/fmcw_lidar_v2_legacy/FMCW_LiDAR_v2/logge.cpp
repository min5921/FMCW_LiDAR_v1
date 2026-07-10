#include "logger.h"
#include <vector>
#include <mutex>
#include <cstdarg>
#include <cstdio>

static std::vector<std::string> g_log;
static std::mutex g_log_mtx;

void AddLog(const char* fmt, ...)
{
    char buf[1024];

    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    std::lock_guard<std::mutex> lock(g_log_mtx);
    g_log.emplace_back(buf);
}

void ClearLogs()
{
    std::lock_guard<std::mutex> lock(g_log_mtx);
    g_log.clear();
}

int GetLogCount()
{
    std::lock_guard<std::mutex> lock(g_log_mtx);
    return (int)g_log.size();
}

const char* GetLogLine(int index)
{
    std::lock_guard<std::mutex> lock(g_log_mtx);
    if (index < 0 || index >= (int)g_log.size())
        return "";
    return g_log[index].c_str();
}
