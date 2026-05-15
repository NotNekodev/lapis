#include "util/spinlock.h"
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

void e9_sink_write(const char *data, size_t len) {
    while (len--) {
        _outb(0xe9, *data++);
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