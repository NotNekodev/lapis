#include "kterm/kterm.h"
#include "log/sink.h"
#include <log/sinks/kterm.h>

log_sink_t kterm_sink = {
    .id = -1,
    .level_mask = (1 << LOG_LEVEL_INFO) | (1 << LOG_LEVEL_WARN) | (1 << LOG_LEVEL_ERROR) | (1 << LOG_LEVEL_CRITICAL),
    .name = KTERM_SINK_NAME,
    .write = kterm_sink_write,
    .flush = kterm_sink_flush,
    .private = NULL,
    .next = NULL
};

void kterm_sink_write(const char *data, size_t len) {
    for (size_t i = 0; i < len; i++) {
        kterm_putc(data[i]);
    }
}

void kterm_sink_flush(void) {
    ; // no op
}

int kterm_sink_init(void) {
    return register_sink(&kterm_sink);
}