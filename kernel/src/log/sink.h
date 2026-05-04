#ifndef _SINK_H
#define _SINK_H 1

#include <stddef.h>

#define LOG_SINK_LEVEL_DEBUG    0
#define LOG_SINK_LEVEL_INFO     1
#define LOG_SINK_LEVEL_WARN     2
#define LOG_SINK_LEVEL_ERROR    3
#define LOG_SINK_LEVEL_CRITICAL 4

typedef struct log_sink {
    int id;
    int level_mask; // bitmask of levels to log

    char name[32]; // readable identifier

    void (*write)(const char *data, size_t len);
    void (*flush)(void);

    void* private;

    struct log_sink *next;
} log_sink_t;

int register_sink(log_sink_t *sink); // returns the id of the registered sink, or -1 on failure
void unregister_sink(int id);

void log_to_sinks(const char *data, size_t len, int level);

int get_id_by_name(const char *name);

#endif // _SINK_H