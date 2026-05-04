#ifndef _LOG_H
#define _LOG_H

#define LOG_LEVEL_DEBUG    0
#define LOG_LEVEL_INFO     1
#define LOG_LEVEL_WARN     2
#define LOG_LEVEL_ERROR    3
#define LOG_LEVEL_CRITICAL 4

#define debug(format, ...)      log(LOG_LEVEL_DEBUG, format, ##__VA_ARGS__)
#define info(format, ...)       log(LOG_LEVEL_INFO, format, ##__VA_ARGS__)
#define warn(format, ...)       log(LOG_LEVEL_WARN, format, ##__VA_ARGS__)
#define error(format, ...)      log(LOG_LEVEL_ERROR, format, ##__VA_ARGS__)
#define critical(format, ...)   log(LOG_LEVEL_CRITICAL, format, ##__VA_ARGS__)

void log(int level, const char *format, ...);

#endif // _LOG_H