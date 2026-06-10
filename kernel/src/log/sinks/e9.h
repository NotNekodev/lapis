#ifndef _E9_SINK_H
#define _E9_SINK_H 1

#include <log/sink.h>

#define E9_SINK_NAME "e9"

extern log_sink_t e9_sink;

void e9_sink_write(const char *data, size_t len);
void e9_sink_flush(void);

int e9_sink_init(void);

#endif // _E9_SINK_H