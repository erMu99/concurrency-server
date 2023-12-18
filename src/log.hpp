#pragma once
#include <cstdio>
#include <ctime>

enum
{
    INF = 0,
    DBG,
    ERR,
    LOG_LEVEL = DBG
};
#define LOG(level, format, ...)                                                            \
    do                                                                                     \
    {                                                                                      \
        if (level < LOG_LEVEL)                                                             \
            break;                                                                         \
        time_t t = time(NULL);                                                             \
        struct tm *local = localtime(&t);                                                  \
        char tmp[32] = {0};                                                                \
        strftime(tmp, sizeof(tmp) - 1, "%H:%M:%S", local);                                 \
        fprintf(stdout, "[%s %s:%d]" format "\n", tmp, __FILE__, __LINE__, ##__VA_ARGS__); \
    } while (0)
#define INF_LOG(format, ...) LOG(INF, format, ##__VA_ARGS__)
#define DBG_LOG(format, ...) LOG(DBG, format, ##__VA_ARGS__)
#define ERR_LOG(format, ...) LOG(ERR, format, ##__VA_ARGS__)