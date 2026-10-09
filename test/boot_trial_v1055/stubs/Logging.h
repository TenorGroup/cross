#pragma once
template<class... Args> void hostLog(const char*, const char*, Args...) {}
#define LOG_INF(...) hostLog(__VA_ARGS__)
#define LOG_ERR(...) hostLog(__VA_ARGS__)
