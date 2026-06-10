#include <log/sinks/e9.h>

#include <arch/io.h>

#include <log/sink.h>
#include <log/log.h>

log_sink_t e9_sink = {
    .id = -1,
    .level_mask = PRIMARY_EARLY_BOOT_SINK_LEVEL_MASK_FLAGS,
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
    ; // no op
}

int e9_sink_init(void) {
    return register_sink(&e9_sink);
}