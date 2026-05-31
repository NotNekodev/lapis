#include "pit.h"
#include "arch/io.h"
#include "arch/interrupts/apic.h"
#include "log/log.h"

#define PIT_CMD  0x43
#define PIT_CH0  0x40

static volatile uint64_t g_pit_ticks = 0;

uint64_t pit_get_ticks(void) {
    return g_pit_ticks;
}

void pit_init(uint32_t hz) {
    if (hz == 0)
        hz = 100;

    uint16_t reload = (uint16_t)(PIT_FREQUENCY / hz);

    _outb(PIT_CMD, 0x34); // channel 0, lobyte/hibyte, mode 2
    _outb(PIT_CH0, reload & 0xFF);
    _outb(PIT_CH0, reload >> 8);

    info("pit: initialized at %u Hz (reload=%u)\n", hz, reload);
}

void pit_stop(void) {
    _outb(PIT_CMD, 0x30);
    _outb(PIT_CH0, 0);
    _outb(PIT_CH0, 0);
}

void pit_sleep(uint64_t ticks) {
    uint64_t start = g_pit_ticks;
    while ((g_pit_ticks - start) < ticks) {
        _pause();
    }
}

void pit_irq_handler(void) {
    g_pit_ticks++;
    apic_eoi(0);
}