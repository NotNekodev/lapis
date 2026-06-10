#ifndef _KTERM_SINK_H
#define _KTERM_SINK_H

#include <stddef.h>

#define KTERM_SINK_NAME "kterm"

void kterm_sink_write(const char *data, size_t len);
void kterm_sink_flush(void);

int kterm_sink_init(void);

#endif // _KTERM_SINK_H