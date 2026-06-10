#ifndef UART16550_H
#define UART16550_H 1

#include <stddef.h>

#define UART_BASE 0x3f8
#define UART_SINK_NAME "uart16550"
#define UART_THR 0
#define UART_DLL 0
#define UART_DLM 1
#define UART_IER 1
#define UART_FCR 2
#define UART_LCR 3
#define UART_LSR 5 

#define UART_LSR_THRE 0x20 // transmitter holding register empty
#define UART_LSR_TEMT 0x40 // transmitter shift register empty

void uart_sink_write(const char *data, size_t len);
void uart_sink_flush(void);

int uart_sink_init(void);

#endif // UART16550_H
