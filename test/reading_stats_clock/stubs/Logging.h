#pragma once

void testLog(const char* tag, const char* format, ...);

#define LOG_INF testLog
#define LOG_ERR testLog
#define LOG_DBG testLog
