#ifndef _SINK_H
#define _SINK_H 1

#include <stddef.h>
#include "log.h"

typedef struct log_sink {
    int id;
    int level_mask; // bitmask of levels to log

    char name[32]; // readable identifier

    void (*write)(const char *data, size_t len);
    void (*flush)(void);

    void* private;

    struct log_sink *next;
} log_sink_t;

static inline void pretty_sink_write(log_sink_t *sink, const char *data, size_t len, int level) {
    output_log_prefix(sink->write, level);
    sink->write(data, len);
    output_log_suffix(sink->write, level);
}

int register_sink(log_sink_t *sink); // returns the id of the registered sink, or -1 on failure
void unregister_sink(int id);

void log_to_sinks(const char *data, size_t len, int level);
void log_to_sinks_unlocked(const char *data, size_t len, int level); // used for panics, where locking will just get into the way
int get_id_by_name(const char *name);

#endif // _SINK_H