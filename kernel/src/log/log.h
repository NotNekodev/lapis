#ifndef _LOG_H
#define _LOG_H 1

#define LOG_LEVEL_DEBUG    0
#define LOG_LEVEL_INFO     1
#define LOG_LEVEL_WARN     2
#define LOG_LEVEL_ERROR    3
#define LOG_LEVEL_CRITICAL 4

#define LOG_LABEL_DEBUG_PREFIX "\033[1;35mdbg\033[0m:  ", 17
#define LOG_LABEL_INFO_PREFIX  "\033[1;34minfo\033[0m: ", 17
#define LOG_LABEL_WARN_PREFIX  "\033[1;33mwarn\033[0m: ", 17
#define LOG_LABEL_ERROR_PREFIX "\033[1;31merr\033[0m:  ", 17

#define LOG_LABEL_CRIT_PREFIX "\033[1;31mcrit: ", 12
#define LOG_LABEL_CRIT_SUFFIX "\033[0m", 4

#define debug(format, ...)      log(LOG_LEVEL_DEBUG, format, ##__VA_ARGS__)
#define info(format, ...)       log(LOG_LEVEL_INFO, format, ##__VA_ARGS__)
#define warn(format, ...)       log(LOG_LEVEL_WARN, format, ##__VA_ARGS__)
#define error(format, ...)      log(LOG_LEVEL_ERROR, format, ##__VA_ARGS__)
#define critical(format, ...)   log(LOG_LEVEL_CRITICAL, format, ##__VA_ARGS__)

#define PRIMARY_EARLY_BOOT_SINK_LEVEL_MASK_FLAGS ((1 << LOG_LEVEL_DEBUG) | (1 << LOG_LEVEL_INFO) | (1 << LOG_LEVEL_WARN) | (1 << LOG_LEVEL_ERROR) | (1 << LOG_LEVEL_CRITICAL))

void log(int level, const char *format, ...);

static inline void output_log_prefix(void (*out_fn)(const char*, int), int level) {
    switch (level) {
        case LOG_LEVEL_DEBUG:
            out_fn(LOG_LABEL_DEBUG_PREFIX);
            break;
        case LOG_LEVEL_INFO:
            out_fn(LOG_LABEL_INFO_PREFIX);
            break;
        case LOG_LEVEL_WARN:
            out_fn(LOG_LABEL_WARN_PREFIX);
            break;
        case LOG_LEVEL_ERROR:
            out_fn(LOG_LABEL_ERROR_PREFIX);
            break;
        case LOG_LEVEL_CRITICAL:
            out_fn(LOG_LABEL_CRIT_PREFIX);
            break;
        default:
            break;
    }
}

static inline void output_log_suffix(void (*out_fn)(const char*, int), int level) {
    switch (level) {
        case LOG_LEVEL_CRITICAL:
            out_fn(LOG_LABEL_CRIT_SUFFIX);
            break;
        default:
            break;
    }
}

#endif // _LOG_H