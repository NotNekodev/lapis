#include <log/log.h>

#include <log/sink.h>

#define NANOPRINTF_USE_FIELD_WIDTH_FORMAT_SPECIFIERS     1
#define NANOPRINTF_USE_PRECISION_FORMAT_SPECIFIERS       1
#define NANOPRINTF_USE_FLOAT_FORMAT_SPECIFIERS           0
#define NANOPRINTF_USE_SMALL_FORMAT_SPECIFIERS           1
#define NANOPRINTF_USE_LARGE_FORMAT_SPECIFIERS           1
#define NANOPRINTF_USE_BINARY_FORMAT_SPECIFIERS          1
#define NANOPRINTF_USE_WRITEBACK_FORMAT_SPECIFIERS       0
#define NANOPRINTF_SNPRINTF_SAFE_TRIM_STRING_ON_OVERFLOW 1

#define NANOPRINTF_IMPLEMENTATION
#include <log/nanoprintf.h>

#include <stdarg.h>

void log(int level, const char *format, ...) {
    char buffer[1024];
    va_list args;
    va_start(args, format);

    int len = npf_vsnprintf(buffer, sizeof(buffer), format, args);
    va_end(args);

    log_to_sinks(buffer, (size_t)len, level);
}