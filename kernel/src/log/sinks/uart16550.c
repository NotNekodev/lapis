#include <log/sinks/uart16550.h>

#include <log/sink.h>
#include <log/log.h>
#include <arch/io.h>
#include <stddef.h>

log_sink_t uart_sink = {
    .id = -1,
    .level_mask = PRIMARY_EARLY_BOOT_SINK_LEVEL_MASK_FLAGS,
    .name = UART_SINK_NAME,
    .write = uart_sink_write,
    .flush = uart_sink_flush,
    .private = NULL,
    .next = NULL
};

static inline void uart_wait_for_status(unsigned char mask) {
    while (!(_inb(UART_BASE + UART_LSR) & mask));
}

static inline void uart_putc(char c) {
    if (c == '\n') {
        uart_wait_for_status(UART_LSR_THRE);
        _outb(UART_BASE + UART_THR, '\r');
    }

    uart_wait_for_status(UART_LSR_THRE);
    _outb(UART_BASE + UART_THR, c);
}

static void uart_output_string(const char *str, int len) {
    while (len--) {
        uart_putc(*str++);
    }
}

void uart_sink_write(const char *data, size_t len) {
    uart_output_string(data, len);
}

void uart_sink_flush(void) {
    uart_wait_for_status(UART_LSR_THRE | UART_LSR_TEMT);
}

int uart_sink_init(void) {
    _outb(UART_BASE + UART_IER, 0x00);
    _outb(UART_BASE + UART_LCR, 0x80);
    _outb(UART_BASE + UART_DLL, 0x01);
    _outb(UART_BASE + UART_DLM, 0x00);
    _outb(UART_BASE + UART_LCR, 0x03);
    _outb(UART_BASE + UART_FCR, 0x07);

    return register_sink(&uart_sink);
}
