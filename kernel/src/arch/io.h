#ifndef _IO_H
#define _IO_H 1

#include <stdint.h>

void _hlt(void);
void _sti(void);
void _cli(void);

void _outb(uint16_t port, uint8_t value);

#define hcf() do { \
    _cli(); \
    _hlt(); \
} while (0)


#endif // _IO_H