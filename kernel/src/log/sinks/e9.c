#include "log/log.h"
#include <log/sinks/e9.h>

#include <arch/io.h>
#include <log/sink.h>

log_sink_t e9_sink = {
    .id = -1,
    .level_mask = (1 << LOG_SINK_LEVEL_DEBUG) | (1 << LOG_SINK_LEVEL_INFO) | (1 << LOG_SINK_LEVEL_WARN) | (1 << LOG_SINK_LEVEL_ERROR) | (1 << LOG_SINK_LEVEL_CRITICAL),
    .name = E9_SINK_NAME,
    .write = e9_sink_write,
    .flush = e9_sink_flush,
    .private = NULL,
    .next = NULL
};

static inline void e9_output_string(const char *str, int len) {
    while (len--) {
        _outb(0xe9, *str++);
    }
}

void e9_sink_write(const char *data, size_t len, int level) {
    switch (level) {
        case LOG_LEVEL_DEBUG:
            e9_output_string("\033[1;35m", 7);
            e9_output_string("dbg", 3);
            e9_output_string("\033[0m", 4);
            e9_output_string(":  ", 3);
            e9_output_string(data, len);
            break;
        case LOG_LEVEL_INFO:
            e9_output_string("\033[1;34m", 7);
            e9_output_string("info", 4);
            e9_output_string("\033[0m", 4);
            e9_output_string(": ", 2);
            e9_output_string(data, len);
            break;
        case LOG_LEVEL_WARN:
            e9_output_string("\033[1;33m", 7);
            e9_output_string("warn", 4);
            e9_output_string("\033[0m", 4);
            e9_output_string(": ", 2);
            e9_output_string(data, len);
            break;
        case LOG_LEVEL_ERROR:
            e9_output_string("\033[1;31m", 7);
            e9_output_string("err", 3);
            e9_output_string("\033[0m", 4);
            e9_output_string(":  ", 3);
            e9_output_string(data, len);
            break;
        case LOG_LEVEL_CRITICAL:
            e9_output_string("\033[1;31m", 7);
            e9_output_string("crit", 4);
            e9_output_string(": ", 2);
            e9_output_string(data, len);
            e9_output_string("\033[0m", 4);
            break;
    }
}

void e9_sink_flush(void) {
    ;;
}

int e9_sink_init(void) {
    int id = register_sink(&e9_sink);
    if (id < 0) {
        return -1;
    }
    return 0;
}