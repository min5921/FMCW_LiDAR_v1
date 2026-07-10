#pragma once
#include <string>

void AddLog(const char* fmt, ...);
void ClearLogs();
int  GetLogCount();
const char* GetLogLine(int index);
